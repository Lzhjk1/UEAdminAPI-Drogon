"""内置 AI 代理的端到端联调（不访问真实厂商）。

启动一个 OpenAI 兼容的 mock 上游 + 临时后端实例：
  - BaseUrl 设为 http://127.0.0.1:<mock>/plan/v3（模拟腾讯云 Token Plan）
  - 断言代理请求路径、Bearer Key、model、tools、stream=false
  - 断言响应透传、usage 记账、月度额度
"""
from __future__ import annotations

import json
import os
import re
import socket
import subprocess
import sys
import threading
import time
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import pytest

from api_client import ApiClient
from conftest import _psql, _sql_lit

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "build" / "UEAdminAPI_drogon"
CONFIG = ROOT / "config.yaml"

pytestmark = pytest.mark.skipif(
    not BINARY.is_file() or not CONFIG.is_file() or not sys.platform.startswith("linux"),
    reason="需要 Linux 已编译后端与 config.yaml",
)


class _MockUpstream(BaseHTTPRequestHandler):
    requests: list[dict] = []
    mode = "ok"

    def do_POST(self):  # noqa: N802
        length = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(length)
        body = json.loads(raw.decode("utf-8") or "{}")
        type(self).requests.append({
            "path": self.path,
            "authorization": self.headers.get("Authorization") or "",
            "body": body,
        })
        if type(self).mode == "quota":
            data = json.dumps({
                "error": {"code": "quota_exceeded", "message": "套餐额度不足"},
            }).encode("utf-8")
            self.send_response(429)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        payload = {
            "id": "chatcmpl-test",
            "object": "chat.completion",
            "model": body.get("model") or "test-model",
            "choices": [{
                "index": 0,
                "message": {"role": "assistant", "content": "pong"},
                "finish_reason": "stop",
            }],
            "usage": {
                "prompt_tokens": 11,
                "completion_tokens": 5,
                "total_tokens": 16,
            },
        }
        data = json.dumps(payload).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *_args):
        return


def _free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def _wait_ping(base_url: str, timeout: float = 20.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(base_url + "/api/system/ping", timeout=1) as resp:
                if resp.status == 200:
                    return
        except Exception:
            time.sleep(0.2)
    raise RuntimeError("临时后端未在 20 秒内启动")


def _listener_config(port: int) -> str:
    lines = CONFIG.read_text(encoding="utf-8").splitlines()
    in_listener = False
    for index, line in enumerate(lines):
        if line.strip().startswith("listeners:"):
            in_listener = True
            continue
        if in_listener and re.match(r"^\s*port:\s*\d+", line):
            lines[index] = re.sub(r"\d+", str(port), line, count=1)
            break
    return "\n".join(lines) + "\n"


def _set_subscription(team_id: int):
    _psql(f"delete from team_subscription where team_id = {team_id}")
    _psql(
        "insert into team_subscription(team_id, product, plan, start_date, end_date, status, is_trial) "
        f"values ({team_id}, 'ueaicad', 'plus', "
        "(now() at time zone 'Asia/Shanghai')::date - 1, "
        "(now() at time zone 'Asia/Shanghai')::date + 365, 1, false)"
    )


def test_ai_proxy_token_plan_roundtrip(seeded_org, tmp_path):
    _MockUpstream.requests = []
    upstream = ThreadingHTTPServer(("127.0.0.1", 0), _MockUpstream)
    upstream_port = int(upstream.server_address[1])
    thread = threading.Thread(target=upstream.serve_forever, daemon=True)
    thread.start()

    port = _free_port()
    (tmp_path / "config.yaml").write_text(_listener_config(port), encoding="utf-8")
    env = dict(os.environ)
    env.update({
        "UE_AI_ENABLED": "1",
        "UE_AI_BASE_URL": f"http://127.0.0.1:{upstream_port}/plan/v3",
        "UE_AI_KEY": "sk-tp-test",
        "UE_AI_MODEL": "test-model",
        "UE_AI_MAX_TOKENS": "512",
    })
    proc = subprocess.Popen(
        [str(BINARY)], cwd=str(tmp_path), env=env,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    base_url = f"http://127.0.0.1:{port}"
    try:
        _wait_ping(base_url)
        _set_subscription(seeded_org.team_id)

        client = ApiClient(base_url)
        login = client.login_pwd(seeded_org.admin_name, seeded_org.admin_password)
        assert login.get("code") == 0, login

        tools = [{
            "type": "function",
            "function": {
                "name": "ping",
                "description": "ping",
                "parameters": {"type": "object", "properties": {}},
            },
        }]
        body = client._request(
            "POST", "/api/ai/chat",
            json_body={
                "messages": [{"role": "user", "content": "ping"}],
                "tools": tools,
                "tool_choice": "auto",
            },
            check=False,
        )
        assert body.get("code") == 0, body
        assert body["data"]["choices"][0]["message"]["content"] == "pong"

        assert len(_MockUpstream.requests) == 1
        captured = _MockUpstream.requests[0]
        assert captured["path"] == "/plan/v3/chat/completions"
        assert captured["authorization"] == "Bearer sk-tp-test"
        assert captured["body"]["model"] == "test-model"
        assert captured["body"]["stream"] is False
        assert captured["body"]["tools"] == tools
        assert captured["body"]["tool_choice"] == "auto"
        assert captured["body"]["max_tokens"] == 512

        usage = client._request("GET", "/api/ai/usage", check=False)
        assert usage.get("code") == 0, usage
        assert usage["data"]["self"]["totalTokens"] == 16
        assert usage["data"]["quota"]["monthlyTokens"] == 400000000
        assert usage["data"]["quota"]["monthlyCredits"] == 4000

        # 腾讯云套餐额度不足/限流：透传渠道错误，不记用量
        _MockUpstream.mode = "quota"
        failed = client._request(
            "POST", "/api/ai/chat",
            json_body={"messages": [{"role": "user", "content": "ping"}]},
            check=False,
        )
        assert failed.get("code") == -908, failed
        assert "套餐额度不足" in (failed.get("msg") or ""), failed
        _MockUpstream.mode = "ok"
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
        upstream.shutdown()
        upstream.server_close()

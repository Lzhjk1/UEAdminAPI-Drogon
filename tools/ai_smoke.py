#!/usr/bin/env python3
"""内置 AI 模型联通自检（不经过数据库/账号）。

用法：
  python tools/ai_smoke.py
  python tools/ai_smoke.py --tools
  UE_AI_KEY=sk-tp-xxx python tools/ai_smoke.py --model hy4-flash

默认读取仓库根目录 config.yaml 的 custom_config.AI；环境变量
UE_AI_BASE_URL / UE_AI_KEY / UE_AI_MODEL / UE_AI_ENABLED 可覆盖。
只发起一次极小的 chat/completions 请求，用于购买 Token 套餐后确认地址、Key、模型三项正确。
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path


def _scalar(value: str) -> str:
    value = value.strip()
    if " #" in value:
        value = value.split(" #", 1)[0].strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
        value = value[1:-1]
    return value


def load_ai_config(path: Path) -> dict:
    data: dict[str, str] = {}
    if not path.is_file():
        return data
    in_ai = False
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if re.match(r"^  AI:\s*$", raw):
            in_ai = True
            continue
        if not in_ai:
            continue
        if re.match(r"^  \S", raw):
            break
        match = re.match(r"^    ([A-Za-z][A-Za-z0-9_]*):\s*(.*)$", raw)
        if match:
            data[match.group(1).lower()] = _scalar(match.group(2))
    return data


def main() -> int:
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass

    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description="UEAICAD 内置 AI 联通自检")
    parser.add_argument("--config", default=str(root / "config.yaml"))
    parser.add_argument("--base-url", default="")
    parser.add_argument("--key", default="")
    parser.add_argument("--model", default="")
    parser.add_argument("--prompt", default="只回复：pong")
    parser.add_argument("--tools", action="store_true", help="附带一个 ping 工具，检查 Function Calling 是否被接受")
    parser.add_argument("--timeout", type=float, default=60.0)
    args = parser.parse_args()

    cfg = load_ai_config(Path(args.config))
    base_url = (args.base_url or os.environ.get("UE_AI_BASE_URL")
                or cfg.get("baseurl", "")).strip()
    api_key = (args.key or os.environ.get("UE_AI_KEY")
               or cfg.get("apikey", "")).strip()
    model = (args.model or os.environ.get("UE_AI_MODEL")
             or cfg.get("model", "")).strip()

    enabled_raw = os.environ.get("UE_AI_ENABLED", cfg.get("enabled", "true")).strip().lower()
    enabled = enabled_raw in ("1", "true", "yes", "on")

    if not enabled:
        print("AI.Enabled=false（或 UE_AI_ENABLED=0），请先启用")
        return 2
    if not base_url:
        print("未配置 AI.BaseUrl / UE_AI_BASE_URL")
        return 2
    if not api_key:
        print("未配置 AI.ApiKey / UE_AI_KEY（Token Plan 的 Key 形如 sk-tp-...）")
        return 2
    if not model:
        print("未配置 AI.Model / UE_AI_MODEL")
        return 2

    endpoint = base_url.rstrip("/")
    if not endpoint.endswith("/chat/completions"):
        endpoint += "/chat/completions"

    body = {
        "model": model,
        "messages": [{"role": "user", "content": args.prompt}],
        "stream": False,
        "temperature": 0.1,
        "max_tokens": 64,
    }
    if args.tools:
        body["tools"] = [{
            "type": "function",
            "function": {
                "name": "ping",
                "description": "返回 pong；用于检查工具调用能力",
                "parameters": {"type": "object", "properties": {}},
            },
        }]
        body["tool_choice"] = "auto"

    request = urllib.request.Request(
        endpoint,
        data=json.dumps(body, ensure_ascii=False).encode("utf-8"),
        method="POST",
        headers={
            "Authorization": "Bearer " + api_key,
            "Content-Type": "application/json",
            "Accept": "application/json",
        },
    )
    print("endpoint:", endpoint)
    print("model:   ", model)
    print("tools:   ", "on" if args.tools else "off")
    try:
        with urllib.request.urlopen(request, timeout=args.timeout) as response:
            status = response.status
            raw = response.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as exc:
        status = exc.code
        raw = exc.read().decode("utf-8", errors="replace")
    except Exception as exc:  # noqa: BLE001
        print("请求失败:", exc)
        return 1

    print("HTTP:    ", status)
    try:
        payload = json.loads(raw)
    except ValueError:
        print("返回非 JSON:", raw[:500])
        return 1

    if status < 200 or status >= 300 or payload.get("error"):
        error = payload.get("error")
        if isinstance(error, dict):
            print("错误:", error.get("code") or error.get("type") or "", error.get("message") or "")
        else:
            print("错误:", json.dumps(error, ensure_ascii=False) if error else raw[:500])
        return 1

    choices = payload.get("choices") or []
    message = (choices[0] or {}).get("message") if choices else {}
    content = (message or {}).get("content") or ""
    tool_calls = (message or {}).get("tool_calls") or []
    print("model:   ", payload.get("model") or "")
    print("usage:   ", json.dumps(payload.get("usage") or {}, ensure_ascii=False))
    print("content: ", str(content)[:200].replace("\n", " "))
    print("toolCalls:", len(tool_calls))
    if not choices:
        print("警告: 响应没有 choices")
        return 1
    print("OK: 内置模型链路可用")
    return 0


if __name__ == "__main__":
    sys.exit(main())

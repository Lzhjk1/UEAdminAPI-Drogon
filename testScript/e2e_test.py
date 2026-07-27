#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
端到端验证脚本: PDMS 客户端 -> 服务端 -> SQLite
模拟 UEHttpConnection 的调用链, 验证 SQL RPC 全链路.

使用方法:
  python e2e_test.py [--base-url http://127.0.0.1:6666]
"""

import sys
import os
import json
import time
import hmac
import hashlib
import base64
import argparse
import ssl
try:
    import urllib.request
    import urllib.error
except ImportError:
    print("需要 Python 3")
    sys.exit(1)

# 跳过 HTTPS 自签名证书验证
_ssl_ctx = ssl.create_default_context()
_ssl_ctx.check_hostname = False
_ssl_ctx.verify_mode = ssl.CERT_NONE

# ---- JWT 生成 (不依赖 PyJWT) ----

JWT_SECRET = "UEAdminAPISecert_Porthack_16"
JWT_ISSUER = "UEAdminAPI"

def base64url_encode(data):
    return base64.urlsafe_b64encode(data).rstrip(b'=').decode('ascii')

def create_jwt(user_id=1, status=1, expires_in_seconds=7200):
    header = {"alg": "HS512", "typ": "JWS"}
    now = int(time.time())
    payload = {
        "iss": JWT_ISSUER,
        "id": str(user_id),
        "tokenType": "token",
        "status": str(status),
        "iat": now,
        "exp": now + expires_in_seconds
    }
    header_json = json.dumps(header, separators=(',', ':')).encode('utf-8')
    payload_json = json.dumps(payload, separators=(',', ':')).encode('utf-8')
    header_b64 = base64url_encode(header_json)
    payload_b64 = base64url_encode(payload_json)
    signing_input = (header_b64 + '.' + payload_b64).encode('ascii')
    signature = hmac.new(JWT_SECRET.encode('utf-8'), signing_input, hashlib.sha512).digest()
    sig_b64 = base64url_encode(signature)
    return header_b64 + '.' + payload_b64 + '.' + sig_b64

# ---- HTTP 客户端 ----

class E2ETestClient:
    def __init__(self, base_url="https://127.0.0.1:6666"):
        self.base_url = base_url.rstrip('/')
        self.token = create_jwt()
        self.passed = 0
        self.failed = 0
        self.errors = []

    def _post(self, path, body_dict):
        url = self.base_url + path
        body = json.dumps(body_dict, ensure_ascii=False).encode('utf-8')
        req = urllib.request.Request(url, data=body, method='POST')
        req.add_header('Content-Type', 'application/json; charset=utf-8')
        req.add_header('Authorization', 'Bearer ' + self.token)
        try:
            with urllib.request.urlopen(req, timeout=30, context=_ssl_ctx) as resp:
                raw = resp.read().decode('utf-8')
                return json.loads(raw)
        except urllib.error.HTTPError as e:
            raw = e.read().decode('utf-8') if e.fp else ''
            try:
                return json.loads(raw)
            except Exception:
                return {"code": -999, "msg": "HTTP {}: {}".format(e.code, raw[:200])}
        except Exception as e:
            return {"code": -998, "msg": str(e)}

    def _get(self, path):
        url = self.base_url + path
        req = urllib.request.Request(url, method='GET')
        req.add_header('Authorization', 'Bearer ' + self.token)
        try:
            with urllib.request.urlopen(req, timeout=30, context=_ssl_ctx) as resp:
                raw = resp.read().decode('utf-8')
                return json.loads(raw)
        except urllib.error.HTTPError as e:
            raw = e.read().decode('utf-8') if e.fp else ''
            try:
                return json.loads(raw)
            except Exception:
                return {"code": -999, "msg": "HTTP {}: {}".format(e.code, raw[:200])}
        except Exception as e:
            return {"code": -998, "msg": str(e)}

    def check(self, name, condition, detail=""):
        if condition:
            self.passed += 1
            print("  [PASS] {}".format(name))
        else:
            self.failed += 1
            self.errors.append(name)
            print("  [FAIL] {}  {}".format(name, detail))

    def run_all(self):
        print("=" * 60)
        print("端到端验证: PDMS -> 服务端 -> SQLite")
        print("服务端: {}".format(self.base_url))
        print("JWT token: {}...".format(self.token[:40]))
        print("=" * 60)

        # 1. 健康检查
        print("\n--- 1. 健康检查 /system/sqlite/ping ---")
        r = self._get("/system/sqlite/ping")
        print("  响应: {}".format(json.dumps(r, ensure_ascii=False)))
        self.check("sqlite ping 返回 code=0", r.get("code") == 0, str(r))

        # 2. 逻辑名解析
        print("\n--- 2. 逻辑名解析 /api/sqlite/resolve ---")
        r = self._post("/api/sqlite/resolve", {
            "scope": "global",
            "templateKind": "e2e_test"
        })
        print("  响应: {}".format(json.dumps(r, ensure_ascii=False)))
        self.check("resolve 返回 code=0", r.get("code") == 0, str(r))
        logical_name = r.get("data", {}).get("logicalName", "")
        self.check("logicalName = 'global.e2e_test'",
                    logical_name == "global.e2e_test",
                    "实际: '{}'".format(logical_name))
        resolved_by_meta = r.get("data", {}).get("resolvedByMetadata", False)
        self.check("resolvedByMetadata = true", resolved_by_meta == True,
                    "实际: {}".format(resolved_by_meta))

        # 3. 查询测试
        print("\n--- 3. 查询 /api/sqlite/query ---")
        r = self._post("/api/sqlite/query", {
            "logicalName": "global.e2e_test",
            "sql": "SELECT id, name, value, flag FROM test_table ORDER BY id;",
            "params": []
        })
        print("  响应: {}".format(json.dumps(r, ensure_ascii=False)))
        self.check("query 返回 code=0", r.get("code") == 0, str(r))
        rows = r.get("data", {}).get("rows", [])
        self.check("返回 3 行数据", len(rows) == 3,
                    "实际: {} 行".format(len(rows)))
        if len(rows) >= 3:
            self.check("第1行 name=alpha", rows[0].get("name") == "alpha", str(rows[0]))
            self.check("第1行 value=1.5", abs(float(rows[0].get("value", 0)) - 1.5) < 0.01, str(rows[0]))
            self.check("第2行 name=beta", rows[1].get("name") == "beta", str(rows[1]))
            self.check("第3行 flag=1", str(rows[2].get("flag")) in ("1", "True", "true"), str(rows[2]))

        # 4. 执行测试 (INSERT)
        print("\n--- 4. 执行 INSERT /api/sqlite/exec ---")
        r = self._post("/api/sqlite/exec", {
            "logicalName": "global.e2e_test",
            "sql": "INSERT INTO test_table (name, value, flag) VALUES (?, ?, ?);",
            "params": [
                {"type": "text", "value": "delta"},
                {"type": "real", "value": 4.5},
                {"type": "int", "value": 0}
            ]
        })
        print("  响应: {}".format(json.dumps(r, ensure_ascii=False)))
        self.check("exec INSERT 返回 code=0", r.get("code") == 0, str(r))
        affected = r.get("data", {}).get("affectedRows", 0)
        self.check("affectedRows = 1", affected == 1,
                    "实际: {}".format(affected))

        # 5. 验证 INSERT 生效
        print("\n--- 5. 验证 INSERT 生效 ---")
        r = self._post("/api/sqlite/query", {
            "logicalName": "global.e2e_test",
            "sql": "SELECT name FROM test_table WHERE name = 'delta';",
            "params": []
        })
        print("  响应: {}".format(json.dumps(r, ensure_ascii=False)))
        rows = r.get("data", {}).get("rows", [])
        self.check("能查到 delta 行", len(rows) == 1,
                    "实际: {} 行".format(len(rows)))

        # 6. 事务测试 - 回滚
        print("\n--- 6. 事务回滚测试 ---")
        r = self._post("/api/sqlite/tx/begin", {
            "logicalName": "global.e2e_test"
        })
        print("  begin 响应: {}".format(json.dumps(r, ensure_ascii=False)))
        self.check("tx/begin 返回 code=0", r.get("code") == 0, str(r))
        tx_id = r.get("data", {}).get("txId", "")
        self.check("返回 txId 非空", len(tx_id) > 0, "实际: '{}'".format(tx_id))

        if tx_id:
            r = self._post("/api/sqlite/exec", {
                "logicalName": "global.e2e_test",
                "sql": "INSERT INTO test_table (name, value, flag) VALUES (?, ?, ?);",
                "params": [
                    {"type": "text", "value": "temp_rollback"},
                    {"type": "real", "value": 9.9},
                    {"type": "int", "value": 1}
                ],
                "txId": tx_id
            })
            print("  exec(in tx) 响应: {}".format(json.dumps(r, ensure_ascii=False)))
            self.check("事务内 INSERT 返回 code=0", r.get("code") == 0, str(r))

            r = self._post("/api/sqlite/tx/rollback", {
                "txId": tx_id
            })
            print("  rollback 响应: {}".format(json.dumps(r, ensure_ascii=False)))
            self.check("tx/rollback 返回 code=0", r.get("code") == 0, str(r))

            r = self._post("/api/sqlite/query", {
                "logicalName": "global.e2e_test",
                "sql": "SELECT name FROM test_table WHERE name = 'temp_rollback';",
                "params": []
            })
            rows = r.get("data", {}).get("rows", [])
            self.check("回滚后 temp_rollback 不存在", len(rows) == 0,
                        "实际: {} 行".format(len(rows)))

        # 7. 事务测试 - 提交
        print("\n--- 7. 事务提交测试 ---")
        r = self._post("/api/sqlite/tx/begin", {
            "logicalName": "global.e2e_test"
        })
        tx_id = r.get("data", {}).get("txId", "")
        self.check("tx/begin 返回 code=0", r.get("code") == 0, str(r))

        if tx_id:
            r = self._post("/api/sqlite/exec", {
                "logicalName": "global.e2e_test",
                "sql": "INSERT INTO test_table (name, value, flag) VALUES (?, ?, ?);",
                "params": [
                    {"type": "text", "value": "temp_commit"},
                    {"type": "real", "value": 8.8},
                    {"type": "int", "value": 1}
                ],
                "txId": tx_id
            })
            self.check("事务内 INSERT 返回 code=0", r.get("code") == 0, str(r))

            r = self._post("/api/sqlite/tx/commit", {
                "txId": tx_id
            })
            self.check("tx/commit 返回 code=0", r.get("code") == 0, str(r))

            r = self._post("/api/sqlite/query", {
                "logicalName": "global.e2e_test",
                "sql": "SELECT name FROM test_table WHERE name = 'temp_commit';",
                "params": []
            })
            rows = r.get("data", {}).get("rows", [])
            self.check("提交后 temp_commit 存在", len(rows) == 1,
                        "实际: {} 行".format(len(rows)))

        # 8. 参数化查询测试
        print("\n--- 8. 参数化查询测试 ---")
        r = self._post("/api/sqlite/query", {
            "logicalName": "global.e2e_test",
            "sql": "SELECT id, name FROM test_table WHERE flag = ? ORDER BY id;",
            "params": [
                {"type": "int", "value": 1}
            ]
        })
        print("  响应: {}".format(json.dumps(r, ensure_ascii=False)))
        self.check("参数化查询返回 code=0", r.get("code") == 0, str(r))
        rows = r.get("data", {}).get("rows", [])
        self.check("flag=1 的行数 >= 3 (alpha, gamma, temp_commit)",
                    len(rows) >= 3, "实际: {} 行".format(len(rows)))

        # 9. 清理测试数据
        print("\n--- 9. 清理测试数据 ---")
        r = self._post("/api/sqlite/exec", {
            "logicalName": "global.e2e_test",
            "sql": "DELETE FROM test_table WHERE name IN ('delta', 'temp_commit');",
            "params": []
        })
        self.check("清理返回 code=0", r.get("code") == 0, str(r))

        # 汇总
        print("\n" + "=" * 60)
        print("验证结果汇总:")
        print("  通过: {}".format(self.passed))
        print("  失败: {}".format(self.failed))
        if self.errors:
            print("  失败项: {}".format(", ".join(self.errors)))
        print("=" * 60)
        return self.failed == 0


def main():
    parser = argparse.ArgumentParser(description='端到端验证脚本')
    parser.add_argument('--base-url', default='https://127.0.0.1:6666',
                        help='服务端地址 (默认: https://127.0.0.1:6666)')
    args = parser.parse_args()

    client = E2ETestClient(args.base_url)
    success = client.run_all()
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()

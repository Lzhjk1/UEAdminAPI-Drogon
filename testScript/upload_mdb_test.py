#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
端到端测试: 上传 .mdb -> 服务端转 SQLite -> 查询验证
"""
import sys
import os
import json
import ssl
import time
import base64
import hmac
import hashlib
import urllib.request
import urllib.error

_ssl_ctx = ssl.create_default_context()
_ssl_ctx.check_hostname = False
_ssl_ctx.verify_mode = ssl.CERT_NONE

JWT_SECRET = "UEAdminAPISecert_Porthack_16"
JWT_ISSUER = "UEAdminAPI"

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
    def b64(d):
        return base64.urlsafe_b64encode(json.dumps(d, separators=(',', ':')).encode('utf-8')).rstrip(b'=').decode('ascii')
    header_b64 = b64(header)
    payload_b64 = b64(payload)
    signing_input = (header_b64 + '.' + payload_b64).encode('ascii')
    signature = hmac.new(JWT_SECRET.encode('utf-8'), signing_input, hashlib.sha512).digest()
    sig_b64 = base64.urlsafe_b64encode(signature).rstrip(b'=').decode('ascii')
    return header_b64 + '.' + payload_b64 + '.' + sig_b64

def _post(path, body_dict, token):
    url = 'https://127.0.0.1:6666' + path
    body = json.dumps(body_dict, ensure_ascii=False).encode('utf-8')
    req = urllib.request.Request(url, data=body, method='POST')
    req.add_header('Content-Type', 'application/json; charset=utf-8')
    req.add_header('Authorization', 'Bearer ' + token)
    try:
        with urllib.request.urlopen(req, timeout=120, context=_ssl_ctx) as resp:
            return json.loads(resp.read().decode('utf-8'))
    except urllib.error.HTTPError as e:
        raw = e.read().decode('utf-8') if e.fp else ''
        try:
            return json.loads(raw)
        except Exception:
            return {"code": -999, "msg": "HTTP {}: {}".format(e.code, raw[:200])}
    except Exception as e:
        return {"code": -998, "msg": str(e)}

def main():
    print("=" * 60)
    print("端到端测试: 上传 .mdb -> 服务端转 SQLite -> 查询验证")
    print("=" * 60)

    token = create_jwt()
    mdb_path = r'D:\Program Files (x86)\UESoft\UESoft AutoPDMS\11.7.20260525\AutoPDMS\project\塔里木\塔里木000\Admin.mdb'

    passed = 0
    failed = 0
    def check(desc, cond):
        nonlocal passed, failed
        if cond:
            print("  [PASS] {}".format(desc))
            passed += 1
        else:
            print("  [FAIL] {}".format(desc))
            failed += 1

    # --- 1. 读取 .mdb 文件并 base64 编码 ---
    print("\n--- 1. 读取 .mdb 文件 ---")
    if not os.path.exists(mdb_path):
        print("  [SKIP] .mdb 文件不存在: {}".format(mdb_path))
        return 1
    with open(mdb_path, 'rb') as f:
        mdb_bytes = f.read()
    mdb_b64 = base64.b64encode(mdb_bytes).decode('ascii')
    print("  文件大小: {} bytes".format(len(mdb_bytes)))
    print("  base64 大小: {} chars".format(len(mdb_b64)))
    check("读取 .mdb 成功", len(mdb_bytes) > 0)

    # --- 2. 上传 .mdb ---
    print("\n--- 2. 上传 .mdb 到服务端 ---")
    resp = _post('/api/sqlite/upload-mdb', {
        'mdbFileName': 'Admin.mdb',
        'base64Data': mdb_b64,
        'scope': 'global',
        'projectCode': ''
    }, token)
    print("  响应: {}".format(json.dumps(resp, ensure_ascii=False)[:300]))
    check("上传返回 code=0", resp.get("code") == 0)

    logical_name = resp.get("data", {}).get("logicalName", "")
    check("返回 logicalName 非空", len(logical_name) > 0)
    print("  logicalName: {}".format(logical_name))

    if resp.get("code") != 0:
        print("\n上传失败, 终止测试")
        return 1

    # --- 3. 用返回的 logicalName 查询 ---
    print("\n--- 3. 用返回的 logicalName 查询 USER 表 ---")
    resp = _post('/api/sqlite/query', {
        'logicalName': logical_name,
        'sql': 'SELECT "name" FROM "USER" ORDER BY "elementid"',
        'params': []
    }, token)
    print("  响应: {}".format(json.dumps(resp, ensure_ascii=False)[:300]))
    check("查询返回 code=0", resp.get("code") == 0)
    rows = resp.get("data", {}).get("rows", [])
    check("返回 3 行 (SYSTEM/用户A/用户B)", len(rows) == 3)
    if rows:
        names = [r.get("name", "") for r in rows]
        check("包含 /SYSTEM", any("/SYSTEM" in n for n in names))
        check("包含 /用户A", any("用户A" in n for n in names))

    # --- 4. 查询 DB 表 ---
    print("\n--- 4. 查询 DB 表 ---")
    resp = _post('/api/sqlite/query', {
        'logicalName': logical_name,
        'sql': 'SELECT count(*) AS cnt FROM "DB"',
        'params': []
    }, token)
    check("查询返回 code=0", resp.get("code") == 0)
    rows = resp.get("data", {}).get("rows", [])
    if rows:
        check("DB 表 81 行", rows[0].get("cnt") == 81)

    # --- 5. 查询 DBInfo 表 ---
    print("\n--- 5. 查询 DBInfo 表 ---")
    resp = _post('/api/sqlite/query', {
        'logicalName': logical_name,
        'sql': 'SELECT count(*) AS cnt FROM "DBInfo"',
        'params': []
    }, token)
    check("查询返回 code=0", resp.get("code") == 0)
    rows = resp.get("data", {}).get("rows", [])
    if rows:
        check("DBInfo 表 499 行", rows[0].get("cnt") == 499)

    # --- 汇总 ---
    print("\n" + "=" * 60)
    print("测试结果: 通过 {}, 失败 {}".format(passed, failed))
    print("=" * 60)
    return 0 if failed == 0 else 1

if __name__ == '__main__':
    sys.exit(main())

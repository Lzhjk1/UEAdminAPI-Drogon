#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
客户端对接简化测试: 模拟 UEHttpConnection 链路
  PDMS 业务层 Access SQL
    -> UEAccessSqliteDialect::AccessToSQLite (Python 复刻)
    -> HTTPS POST /api/sqlite/query
    -> 服务端 SQLiteService 查询
    -> JSON 响应解析
"""
import sys
import os
import json
import ssl
import re
import time
import urllib.request
import urllib.error
import hmac
import hashlib
import base64

_ssl_ctx = ssl.create_default_context()
_ssl_ctx.check_hostname = False
_ssl_ctx.verify_mode = ssl.CERT_NONE

# ---- JWT 生成 (与 e2e_test.py / AuthService 一致) ----
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

# ---- Access SQL -> SQLite SQL 方言转换 (复刻 UEAccessSqliteDialect::AccessToSQLite) ----
def access_to_sqlite(sql):
    result = sql
    # 1. [field] -> "field" (不在单引号字符串内)
    result = replace_brackets(result)
    # 2. true/false -> 1/0 (大小写不敏感, 不在单引号内)
    result = replace_boolean_literals(result)
    # 3. #date# -> 'date'
    result = replace_date_delimiters(result)
    # 4. SELECT TOP N -> ... LIMIT N
    result = replace_top_clause(result)
    return result

def replace_brackets(sql):
    result = []
    in_quote = False
    i = 0
    while i < len(sql):
        ch = sql[i]
        if ch == "'":
            in_quote = not in_quote
            result.append(ch)
            i += 1
            continue
        if not in_quote and ch == '[':
            j = i + 1
            while j < len(sql) and sql[j] != ']':
                j += 1
            if j < len(sql):
                result.append('"')
                result.append(sql[i+1:j])
                result.append('"')
                i = j + 1
                continue
        result.append(ch)
        i += 1
    return ''.join(result)

def replace_boolean_literals(sql):
    result = []
    in_quote = False
    i = 0
    lower = sql.lower()
    while i < len(sql):
        ch = sql[i]
        if ch == "'":
            in_quote = not in_quote
            result.append(ch)
            i += 1
            continue
        if not in_quote:
            if i + 4 <= len(sql) and lower[i:i+4] == 'true' and (i == 0 or not lower[i-1].isalnum()):
                result.append('1')
                i += 4
                continue
            if i + 5 <= len(sql) and lower[i:i+5] == 'false' and (i == 0 or not lower[i-1].isalnum()):
                result.append('0')
                i += 5
                continue
        result.append(ch)
        i += 1
    return ''.join(result)

def replace_date_delimiters(sql):
    result = []
    in_quote = False
    i = 0
    while i < len(sql):
        ch = sql[i]
        if ch == "'":
            in_quote = not in_quote
            result.append(ch)
            i += 1
            continue
        if not in_quote and ch == '#':
            j = i + 1
            while j < len(sql) and sql[j] != '#':
                j += 1
            if j < len(sql):
                date_str = sql[i+1:j]
                # 处理 m/d/yyyy -> yyyy-mm-dd
                parts = re.split(r'[/\-]', date_str)
                if len(parts) == 3:
                    m, d, y = parts
                    date_str = '%04d-%02d-%02d 00:00:00' % (int(y), int(m), int(d))
                result.append("'" + date_str + "'")
                i = j + 1
                continue
        result.append(ch)
        i += 1
    return ''.join(result)

def replace_top_clause(sql):
    # SELECT TOP N -> SELECT ... LIMIT N
    match = re.match(r'^(\s*select\s+)top\s+(\d+)\s+(.*)$', sql, re.IGNORECASE | re.DOTALL)
    if match:
        return match.group(1) + match.group(3) + ' LIMIT ' + match.group(2)
    return sql

# ---- HTTP 客户端 ----
class HttpTestClient:
    def __init__(self, base_url='https://127.0.0.1:6666', logical_name='global.talimu_admin'):
        self.base_url = base_url.rstrip('/')
        self.token = create_jwt()
        self.logical_name = logical_name
        self.passed = 0
        self.failed = 0

    def _post(self, path, body_dict):
        url = self.base_url + path
        body = json.dumps(body_dict, ensure_ascii=False).encode('utf-8')
        req = urllib.request.Request(url, data=body, method='POST')
        req.add_header('Content-Type', 'application/json; charset=utf-8')
        req.add_header('Authorization', 'Bearer ' + self.token)
        try:
            with urllib.request.urlopen(req, timeout=30, context=_ssl_ctx) as resp:
                return json.loads(resp.read().decode('utf-8'))
        except urllib.error.HTTPError as e:
            raw = e.read().decode('utf-8') if e.fp else ''
            try:
                return json.loads(raw)
            except Exception:
                return {"code": -999, "msg": "HTTP {}: {}".format(e.code, raw[:200])}
        except Exception as e:
            return {"code": -998, "msg": str(e)}

    def query(self, access_sql, params=None):
        """模拟 UEHttpConnection::ExecuteSelectSQL"""
        sqlite_sql = access_to_sqlite(access_sql)
        print("  Access SQL: {}".format(access_sql))
        print("  SQLite SQL: {}".format(sqlite_sql))
        body = {
            "logicalName": self.logical_name,
            "sql": sqlite_sql,
            "params": params or []
        }
        resp = self._post('/api/sqlite/query', body)
        print("  响应: {}".format(json.dumps(resp, ensure_ascii=False)[:200]))
        return resp

    def exec(self, access_sql, params=None):
        """模拟 UEHttpConnection::ExecuteNoSelectSQL"""
        sqlite_sql = access_to_sqlite(access_sql)
        print("  Access SQL: {}".format(access_sql))
        print("  SQLite SQL: {}".format(sqlite_sql))
        body = {
            "logicalName": self.logical_name,
            "sql": sqlite_sql,
            "params": params or []
        }
        resp = self._post('/api/sqlite/exec', body)
        print("  响应: {}".format(json.dumps(resp, ensure_ascii=False)[:200]))
        return resp

    def check(self, desc, condition):
        if condition:
            print("  [PASS] {}".format(desc))
            self.passed += 1
        else:
            print("  [FAIL] {}".format(desc))
            self.failed += 1

def main():
    print("=" * 60)
    print("客户端对接简化测试: Access SQL -> 方言转换 -> 服务端查询")
    print("=" * 60)
    client = HttpTestClient()

    # --- 测试 1: 方括号 [] 转换 + 基础查询 ---
    print("\n--- 1. 方括号转换: SELECT [name] FROM [USER] ---")
    resp = client.query('SELECT [name] FROM [USER] ORDER BY [elementid]')
    client.check("返回 code=0", resp.get("code") == 0)
    rows = resp.get("data", {}).get("rows", [])
    client.check("返回行数 >= 3 (有用户A/用户B/SYSTEM)", len(rows) >= 3)
    if rows:
        names = [r.get("name", "") for r in rows]
        client.check("包含 /SYSTEM", any("/SYSTEM" in n for n in names))

    # --- 测试 2: TOP N -> LIMIT N ---
    print("\n--- 2. TOP N 转换: SELECT TOP 2 [name] FROM [PROJ] ---")
    resp = client.query('SELECT TOP 2 [name] FROM [PROJ]')
    client.check("返回 code=0", resp.get("code") == 0)
    rows = resp.get("data", {}).get("rows", [])
    client.check("返回 2 行 (TOP 2 -> LIMIT 2)", len(rows) == 2)

    # --- 测试 3: 布尔字面量 true/false -> 1/0 ---
    print("\n--- 3. 布尔转换: WHERE [delete_flag] = false ---")
    resp = client.query('SELECT [name] FROM [USER] WHERE [delete_flag] = false')
    client.check("返回 code=0", resp.get("code") == 0)
    rows = resp.get("data", {}).get("rows", [])
    # delete_flag=0 的用户
    client.check("返回未删除用户 (delete_flag=0 -> false)", len(rows) >= 1)

    # --- 测试 4: 日期定界符 #date# -> 'date' ---
    print("\n--- 4. 日期定界符转换: WHERE [create_time] > #1/1/2025# ---")
    resp = client.query('SELECT [name] FROM [USER] WHERE [create_time] > #1/1/2025#')
    client.check("返回 code=0", resp.get("code") == 0)

    # --- 测试 5: 参数化查询 (PDMS 常用 ? 占位符) ---
    print("\n--- 5. 参数化查询: WHERE [elementid] = ? ---")
    resp = client.query('SELECT [name] FROM [USER] WHERE [elementid] = ?',
                        [{"type": "int", "value": 17}])
    client.check("返回 code=0", resp.get("code") == 0)
    rows = resp.get("data", {}).get("rows", [])
    client.check("返回 1 行 (elementid=17)", len(rows) == 1)
    if rows:
        client.check("用户名包含 '用户A'", "用户A" in rows[0].get("name", ""))

    # --- 测试 6: 复杂 Access SQL (多方括号 + WHERE + ORDER BY) ---
    print("\n--- 6. 复杂 SQL: 多方括号 + WHERE + ORDER BY ---")
    resp = client.query('SELECT [envid], [elementid], [name], [stype] FROM [DB] WHERE [stype] = ? ORDER BY [elementid] LIMIT 5',
                        [{"type": "text", "value": "PADD"}])
    client.check("返回 code=0", resp.get("code") == 0)
    rows = resp.get("data", {}).get("rows", [])
    client.check("返回行数 >= 1", len(rows) >= 1)

    # --- 测试 7: DBInfo 表查询 (499 行) ---
    print("\n--- 7. DBInfo 表查询 (验证大表迁移正确性) ---")
    resp = client.query('SELECT count(*) AS cnt FROM [DBInfo]')
    client.check("返回 code=0", resp.get("code") == 0)
    rows = resp.get("data", {}).get("rows", [])
    if rows:
        cnt = rows[0].get("cnt", 0)
        client.check("DBInfo 行数 = 499", cnt == 499)

    # --- 测试 8: 不含 Access 语法的纯 SQL (快速路径) ---
    print("\n--- 8. 纯 SQLite SQL (无 Access 语法, 快速路径) ---")
    resp = client.query('SELECT count(*) AS cnt FROM "USER"')
    client.check("返回 code=0", resp.get("code") == 0)
    rows = resp.get("data", {}).get("rows", [])
    if rows:
        client.check("USER 表行数 = 3", rows[0].get("cnt") == 3)

    # --- 汇总 ---
    print("\n" + "=" * 60)
    print("测试结果汇总: 通过 {}, 失败 {}".format(client.passed, client.failed))
    print("=" * 60)
    return 0 if client.failed == 0 else 1

if __name__ == '__main__':
    sys.exit(main())

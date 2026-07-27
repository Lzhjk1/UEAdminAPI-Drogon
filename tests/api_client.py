"""UEAdminAPI 自动化测试 - HTTP 客户端封装

所有接口返回标准响应 { code, msg, data }, 本封装统一通过 raise_for_status 检查 code,
测试侧可直接使用返回的 data 字段, 也可在需要断言非 0 码时调用不检查版本。
"""
from __future__ import annotations

import json
import time
import uuid
import requests


class ApiError(Exception):
    def __init__(self, code: int, msg: str, data=None):
        super().__init__(f"[{code}] {msg}")
        self.code = code
        self.msg = msg
        self.data = data


class ApiClient:
    def __init__(self, base_url: str, session: requests.Session | None = None):
        self.base_url = base_url.rstrip("/")
        self.session = session or requests.Session()
        # 默认给个唯一标识, 便于并发/隔离
        self._auth_token: str | None = None
        self._flash_token: str | None = None
        self._action_token: str | None = None

    # ---------- 通用请求 ----------
    def _headers(self, extra: dict | None = None) -> dict:
        h = {}
        if self._auth_token:
            h["Authorization"] = f"Bearer {self._auth_token}"
        if self._action_token:
            h["X-Action-Token"] = self._action_token
        if extra:
            h.update(extra)
        return h

    def _request(self, method: str, path: str, *, params=None, json_body=None,
                 headers: dict | None = None, check: bool = True):
        url = f"{self.base_url}{path}"
        resp = self.session.request(method, url, params=params, json=json_body,
                                    headers=self._headers(headers))
        try:
            body = resp.json()
        except Exception:
            raise ApiError(-1, f"非 JSON 响应: HTTP {resp.status_code} {resp.text[:200]}")
        if check and body.get("code", 0) != 0:
            raise ApiError(body.get("code", -1), body.get("msg", ""), body.get("data"))
        return body

    # ---------- 系统 ----------
    def ping(self):
        return self._request("GET", "/api/system/ping")

    # ---------- MFA 验证码 ----------
    def send_mfa(self, target: str, mfa_type: str):
        return self._request("GET", "/api/user/mfa",
                             params={"target": target, "type": mfa_type})

    def check_mfa(self, target: str, mfa_code: str, mfa_type: str):
        return self._request("GET", "/api/user/mfa/check",
                             params={"target": target, "mfaCode": mfa_code, "type": mfa_type})

    def get_latest_code(self, target: str, mfa_type: str):
        """测试模式专用: 通过调试接口查询最新验证码"""
        return self._request("GET", "/api/test/mfa/latest",
                             params={"target": target, "type": mfa_type})

    def fetch_code(self, target: str, mfa_type: str) -> str:
        """便捷组合: 发送验证码后立即查询取回(依赖测试模式)"""
        self.send_mfa(target, mfa_type)
        body = self.get_latest_code(target, mfa_type)
        code = body.get("data", {}).get("code") if isinstance(body.get("data"), dict) else None
        # HttpResult 的 data 可能直接放在 jsondata, 这里兼容两种结构
        if not code:
            code = body.get("code_field") or body.get("data", {}).get("code")
        if not code:
            raise ApiError(-1, f"未能从测试接口取回验证码: {body}")
        return code

    # ---------- ActionToken ----------
    def action_token_anonymous(self, mfa_type: str, mfa_code: str, target: str):
        return self._request("GET", "/api/user/action_token/anonymous",
                             params={"mfaType": mfa_type, "mfaCode": mfa_code, "target": target})

    def action_token(self, mfa_type: str, mfa_code: str | None = None, target: str | None = None):
        return self._request("GET", "/api/user/action_token",
                             params={"mfaType": mfa_type, "mfaCode": mfa_code, "target": target})

    # ---------- 注册 ----------
    def register(self, username: str, password: str, email: str | None = None,
                 phone: str | None = None, **extra):
        body = {"username": username, "password": password, **extra}
        if email:
            body["email"] = email
        if phone:
            body["phone"] = phone  # 注: 注册接口字段名为 phone
        # 调用方应已经把 X-Action-Token 设置在 client 上
        return self._request("POST", "/api/user/create", json_body=body)

    def register_by_phone(self, phone: str):
        return self._request("POST", f"/api/user/create/phone",
                             params={"phone": phone})

    def register_by_email(self, email: str):
        return self._request("POST", f"/api/user/create/email",
                             params={"email": email})

    def check_user_exist(self, target: str):
        return self._request("GET", "/api/user/check_exist", params={"target": target})

    # ---------- 登录 ----------
    def login_pwd(self, username: str, password: str):
        body = self._request("GET", "/api/user/login/pwd",
                             params={"userName": username, "passWord": password})
        self._set_login(body)
        return body

    def login_email(self, email: str, action_token: str):
        self._action_token = action_token
        body = self._request("GET", "/api/user/login/email", params={"email": email})
        self._set_login(body)
        return body

    def login_phone(self, phone: str, action_token: str):
        self._action_token = action_token
        body = self._request("GET", "/api/user/login/phone", params={"phone": phone})
        self._set_login(body)
        return body

    def login_flashtoken(self, flash_token: str):
        body = self._request("GET", "/api/user/login/flashtoken",
                             headers={"AuthorizationFlashToken": flash_token})
        self._set_login(body)
        return body

    def verify_flash_token(self, token: str):
        return self._request("GET", "/api/user/token/verify/flash", params={"flashToken": token})

    def verify_auth_token(self, token: str):
        return self._request("GET", "/api/user/token/verify/auth", params={"token": token})

    def get_self(self):
        return self._request("GET", "/api/user/self")

    def logout(self):
        body = self._request("POST", "/api/user/logout")
        self._auth_token = None
        self._flash_token = None
        return body

    # ---------- 用户管理 ----------
    def update_user(self, **fields):
        return self._request("POST", "/api/user/update", json_body=fields)

    def delete_user(self):
        return self._request("DELETE", "/api/user/delete")

    # ---------- 内部工具 ----------
    def _set_login(self, body):
        data = body.get("data") or {}
        if isinstance(data, dict):
            self._auth_token = data.get("token")
            self._flash_token = data.get("flashToken")

    def set_action_token(self, token: str | None):
        self._action_token = token

    def reset_login_state(self):
        """重置登录态, 防止跨用例污染 (适用于 session 级共享 client)"""
        self._auth_token = None
        self._flash_token = None
        self._action_token = None
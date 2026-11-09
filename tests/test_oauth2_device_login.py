"""OAuth2 RFC 8628 设备登录接口 pytest 测试。

覆盖:
  POST /api/oauth2/device_authorization
  POST /api/oauth2/token
  GET/POST /device
  POST /login
  /.well-known/jwks.json
  POST /api/oauth2/introspect
  POST /api/oauth2/revoke
"""
import time

import pytest

from api_client import ApiError

LOOPBACK = "http://127.0.0.1:45678/cb"
DEVICE_GRANT = "urn:ietf:params:oauth:grant-type:device_code"


def _start(client, redirect_uri=LOOPBACK):
    body = client.device_authorization(redirect_uri)
    assert body["code"] == 0
    return body["data"]


def test_device_authorization_success(client):
    data = _start(client)
    assert data["device_code"]
    assert data["user_code"]
    assert data["verification_uri"].endswith("/device")
    assert data["verification_uri_complete"].startswith(data["verification_uri"] + "?user_code=")
    assert data["expires_in"] > 0
    assert data["interval"] > 0
    # state 不再作为取 token 的凭证出现在响应里
    assert "state" not in data


def test_device_authorization_empty_redirect_uri(client):
    body = client.device_authorization()
    assert body["code"] == 0
    assert body["data"]["device_code"]


def test_device_authorization_rejects_non_loopback(client):
    code, body = client.device_authorization_raw("https://evil.example.com/cb")
    assert code == 400
    assert body["code"] == -703  # ApiError_DeviceLoginRedirectUriNotAllowed


def test_device_authorization_rejects_prefix_bypass(client):
    for evil in (
        "http://127.0.0.1.evil.com/cb",
        "http://localhost.evil.com/cb",
        "http://127.0.0.1@evil.com/cb",
    ):
        code, body = client.device_authorization_raw(evil)
        assert code == 400, evil
        assert body["code"] == -703, evil


def test_device_authorization_allows_ipv6_loopback(client):
    _start(client, "http://[::1]:45678/cb")


def test_device_token_invalid_code(client):
    code, body = client.device_token_raw("device-code-does-not-exist")
    assert code == 400
    assert body["error"] == "expired_token"


def test_device_token_pending(client):
    data = _start(client)
    code, body = client.device_token_raw(data["device_code"])
    assert code == 400
    assert body["error"] == "authorization_pending"


def test_device_token_rejects_wrong_grant_type(client):
    data = _start(client)
    code, body = client.device_token_raw(data["device_code"], grant_type="password")
    assert code == 400
    assert body["error"] == "unsupported_grant_type"


def test_device_page_invalid_user_code(client):
    resp = client.device_login_page("NOPE-NOPE")
    assert resp.status_code == 400
    assert "验证码不存在或已过期" in resp.text


def test_device_page_valid_user_code(client):
    data = _start(client)
    resp = client.device_login_page(data["user_code"])
    assert resp.status_code == 200
    assert data["user_code"] in resp.text


def test_device_page_escapes_redirect_uri(client):
    """redirect_uri 会进入登录页隐藏字段，必须 HTML 转义"""
    malicious = 'http://127.0.0.1/cb"><script>alert(1)</script>'
    data = _start(client, malicious)
    resp = client.device_login_page(data["user_code"])
    assert resp.status_code == 200
    assert "<script>alert(1)</script>" not in resp.text
    assert "&lt;script&gt;" in resp.text


def test_device_password_login_and_token(client, registered_email_account):
    """完整流程：pending -> 密码登录 -> 等待 interval -> token -> 二次失败"""
    account = registered_email_account
    data = _start(client)
    device_code = data["device_code"]
    user_code = data["user_code"]
    interval = data["interval"]

    # 第一次轮询：pending，同时会更新 lastPoll
    code, body = client.device_token_raw(device_code)
    assert code == 400 and body["error"] == "authorization_pending"

    # 模拟用户等待页/输入，按 interval 后再完成登录
    time.sleep(interval + 1)
    resp = client.device_login_pwd(user_code, account.username, account.password)
    assert resp.status_code == 302

    time.sleep(interval + 1)
    body = client.device_token(device_code)
    assert body.get("access_token")
    assert body.get("refresh_token")
    assert body.get("id") == account.user_id
    assert body.get("username") == account.username

    # 一次性：device_code 已消费
    code, body = client.device_token_raw(device_code)
    assert code == 400
    assert body["error"] == "expired_token"


def test_device_login_rejects_redirect_uri_mismatch(client):
    data = _start(client)
    resp = client.device_login_pwd(data["user_code"], "someone", "whatever",
                                   "https://evil.example.com/cb")
    assert resp.status_code == 400
    assert resp.json().get("error") == "invalid_request"


def test_device_login_page_done(client):
    resp = client.device_login_done_page()
    assert resp.status_code == 200
    assert "登录成功" in resp.text


def test_jwks_endpoint(client):
    """JWKS 应返回非空 RSA 公钥"""
    resp = client.oauth2_jwks()
    assert resp.status_code == 200
    body = resp.json()
    assert "keys" in body
    assert body["keys"]
    key = body["keys"][0]
    assert key["kty"] == "RSA"
    assert key["alg"] == "RS256"
    assert key["n"]
    assert key["e"]


def test_introspect_missing_token(client):
    resp = client.oauth2_introspect_raw("")
    assert resp.status_code == 200
    assert resp.json().get("active") is False


def test_introspect_valid_token(client, registered_email_account):
    token = registered_email_account.client._auth_token
    assert token
    resp = client.oauth2_introspect_raw(token)
    assert resp.status_code == 200
    body = resp.json()
    assert body.get("active") is True
    assert body.get("username") == registered_email_account.username
    assert body.get("sub")


def test_revoke_flash_token(client, registered_email_account):
    flash_token = registered_email_account.client._flash_token
    assert flash_token
    resp = client.oauth2_revoke_raw(flash_token)
    assert resp.status_code == 200
    assert resp.json().get("result") == "ok"

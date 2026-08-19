"""OAuth2 设备登录接口 pytest 测试。

覆盖:
  POST /api/oauth2/login/start
  GET  /api/oauth2/login/check
  GET  /login
  POST /login
  /.well-known/jwks.json
  POST /api/oauth2/introspect
  POST /api/oauth2/revoke

需要后端开启 TestMode 且已编译新增的 DeviceLoginOptional 过滤器。
"""
import pytest

from api_client import ApiError


def test_login_start_success(client):
    """发起设备登录会话应返回 state / login_url / expires"""
    body = client.device_login_start("http://127.0.0.1:45678/cb")
    assert body["code"] == 0
    data = body["data"]
    assert data["state"]
    assert data["login_url"].startswith("/login?state=")
    assert "redirect_uri=" in data["login_url"]
    assert data["expires"] == 600  # config UserManage.DeviceLoginSec=600


def test_login_start_empty_redirect_uri(client):
    """redirect_uri 为空时兼容旧 ueloginreturn 流程"""
    body = client.device_login_start()
    assert body["code"] == 0
    data = body["data"]
    assert data["state"]
    assert "redirect_uri=" not in data["login_url"]


def test_login_start_rejects_non_loopback(client):
    """非 loopback redirect_uri 应返回 -703"""
    code, body = client.device_login_start_raw("https://evil.example.com/cb")
    assert code == 400
    assert body["code"] == -703  # ApiError_DeviceLoginRedirectUriNotAllowed


def test_login_check_invalid_state(client):
    """不存在的 state 应返回 -701"""
    code, body = client.device_login_check_raw("state-does-not-exist")
    assert code == 404
    assert body["code"] == -701  # ApiError_DeviceLoginStateInvalid


def test_login_check_pending_then_completed(client, registered_email_account):
    """端到端: 创建会话 -> 未登录 active:false -> 密码登录 -> active:true -> 消费后 -701"""
    account = registered_email_account
    redirect_uri = "http://127.0.0.1:45678/cb"

    start = client.device_login_start(redirect_uri)
    state = start["data"]["state"]

    # 未登录
    body = client.device_login_check(state)
    assert body["code"] == 0
    assert body["data"]["active"] is False

    # 错误密码: 保持未登录
    resp = client.device_login_pwd(state, account.username, "WrongPassword123", redirect_uri)
    assert resp.status_code == 401
    assert resp.json()["code"] == -301
    body = client.device_login_check(state)
    assert body["data"]["active"] is False

    # 正确密码: 302 到 loopback
    resp = client.device_login_pwd(state, account.username, account.password, redirect_uri)
    assert resp.status_code == 302
    assert resp.headers.get("Location", "").startswith(redirect_uri)

    # 登录完成, 一次性取 token
    body = client.device_login_check(state)
    assert body["code"] == 0
    data = body["data"]
    assert data["active"] is True
    assert data["token"]
    assert data["flashToken"]
    assert data["username"] == account.username

    # 二次 check 已消费
    code, body = client.device_login_check_raw(state)
    assert code == 404
    assert body["code"] == -701


def test_login_page_invalid_state(client):
    """GET /login 对无效 state 返回 400 HTML"""
    resp = client.device_login_page("invalid-state-xyz")
    assert resp.status_code == 400
    assert "text/html" in resp.headers.get("Content-Type", "").lower()


def test_login_page_valid_state(client):
    """GET /login 对有效 state 返回 200 HTML"""
    state = client.device_login_start("http://127.0.0.1:45678/cb")["data"]["state"]
    resp = client.device_login_page(state, "http://127.0.0.1:45678/cb")
    assert resp.status_code == 200
    assert "text/html" in resp.headers.get("Content-Type", "").lower()
    assert state in resp.text


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
    """无 token 的 introspection 返回 inactive=false"""
    resp = client.oauth2_introspect_raw("")
    assert resp.status_code == 200
    body = resp.json()
    assert body.get("active") is False


def test_introspect_valid_token(client, registered_email_account):
    """有效 AuthToken 的 introspection 返回 active=true 与用户信息"""
    token = registered_email_account.client._auth_token
    assert token
    resp = client.oauth2_introspect_raw(token)
    assert resp.status_code == 200
    body = resp.json()
    assert body.get("active") is True
    assert body.get("username") == registered_email_account.username
    assert body.get("sub")


def test_revoke_flash_token(client, registered_email_account):
    """吊销 FlashToken 后返回 ok"""
    flash_token = registered_email_account.client._flash_token
    assert flash_token
    resp = client.oauth2_revoke_raw(flash_token)
    assert resp.status_code == 200
    assert resp.json().get("result") == "ok"

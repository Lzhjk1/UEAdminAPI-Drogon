"""第三方登录 + 设备登录 ueadmin_state 透传 pytest 测试。

覆盖:
  GET /api/third/authorization_url
    - 不带 ueadmin_state (旧兼容流程)
    - 带有效 ueadmin_state (透传 & 回显)
    - 带无效 ueadmin_state (应被 DeviceLoginOptional 拦截 -701)

说明: QQ/微信真实 OAuth 回调无法在自动化中完成, 因此只覆盖可确定性验证的授权 URL 生成部分。
"""
import pytest

from api_client import ApiError


def test_authorization_url_without_device_state(client):
    """不带 ueadmin_state 保持旧版兼容: 返回 code/verifyCode/authorizationUrl, state 为空"""
    body = client.third_authorization_url("qq")
    assert body["code"] == 0
    data = body["data"]
    assert data["code"]
    assert data["verifyCode"]
    assert data["authorizationUrl"].startswith("https://")
    assert data.get("state", "") == ""
    assert "ueadmin_state=" not in data["authorizationUrl"]


def test_authorization_url_with_valid_device_state(client):
    """带有效 ueadmin_state: authorizationUrl 追加 ueadmin_state 且 data.state 回显"""
    state = client.device_login_start("http://127.0.0.1:45678/cb")["data"]["state"]
    body = client.third_authorization_url("qq", ueadmin_state=state)
    assert body["code"] == 0
    data = body["data"]
    assert data["code"]
    assert data["verifyCode"]
    assert data["state"] == state
    assert f"ueadmin_state={state}" in data["authorizationUrl"]


def test_authorization_url_with_invalid_device_state(client):
    """带无效/过期 ueadmin_state: DeviceLoginOptional 应返回 -701"""
    code, body = client.third_authorization_url_raw("qq", ueadmin_state="bogus-state")
    assert code == 400
    assert body["code"] == -701  # ApiError_DeviceLoginStateInvalid


def test_authorization_url_unsupported_platform(client):
    """不支持的平台应返回 -501"""
    with pytest.raises(ApiError) as ei:
        client.third_authorization_url("not-a-platform")
    assert ei.value.code == -501

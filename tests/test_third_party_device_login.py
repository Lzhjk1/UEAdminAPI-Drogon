"""第三方登录 + 设备登录 ueadmin_state 关联 pytest 测试。

覆盖:
  GET /api/third/authorization_url
    - 不带 ueadmin_state (旧兼容流程)
    - 带有效 ueadmin_state (记录设备 state, 返回 appId/redirectUri 供页内二维码用)
    - 带无效 ueadmin_state (应被 DeviceLoginOptional 拦截 -701)
  GET /api/third/{platform}  (第三方回调)
    - 带设备会话: 必须走到设备会话分支 (渲染提示页, 绝不能跳 ueloginreturn)
    - 不带设备会话: 保持旧行为 (跳 ueloginreturn, PDMS/ueclient 回归)
  GET /login/done  (登录完成页)

说明: QQ/微信真实 OAuth 回调无法在自动化中完成, 因此回调部分用一个不存在的 code:
CallbackRedirect 调用平台 callBack() 后并不检查其返回值, 所以照样会走到设备会话分支,
足以验证"设备 state 是否真的能在回调时取到"——这正是之前断掉的那一环。
"""
import pytest

from api_client import ApiError

LOOPBACK = "http://127.0.0.1:45678/cb"


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
    """带有效 ueadmin_state: 记录设备 state 并回显, 同时返回页内二维码所需字段。

    设备 state 必须存在服务端(loginValue->deviceState), 不能追加到第三方授权 URL 上 ——
    第三方平台回调只会带回 code/state, 附加的自定义参数会被丢弃。
    """
    state = client.device_login_start(LOOPBACK)["data"]["state"]
    body = client.third_authorization_url("qq", ueadmin_state=state)
    assert body["code"] == 0
    data = body["data"]
    assert data["code"]
    assert data["verifyCode"]
    assert data["state"] == state
    # 不能挂在授权 URL 上(注定回不来); 页面靠 data.code 作为 state 渲染二维码
    assert "ueadmin_state=" not in data["authorizationUrl"]
    # 页内二维码(微信 wxLogin.js)需要的两个公开值
    assert data["appId"]
    assert data["redirectUri"].startswith("http")


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


def test_callback_links_device_session(client):
    """回调必须能取到设备 state, 从而进入设备会话分支 (那一针的回归测试)。

    用一个不存在的第三方 code 触发: callBack() 失败 -> openId 为空 ->
    渲染"无法完成登录"提示页。若设备 state 丢失, 这里会退化成
    ueloginreturn:// 的 PDMS 兜底页, 断言即失败。
    """
    state = client.device_login_start(LOOPBACK)["data"]["state"]
    third_code = client.third_authorization_url("wechat", ueadmin_state=state)["data"]["code"]

    resp = client.third_callback_raw("wechat", code="bogus-code", state=third_code)
    assert resp.status_code == 200
    assert "无法完成登录" in resp.text
    # 关键: 带设备会话时绝不能跳自定义协议 (跳了客户端就永远等不到 token)
    assert "ueloginreturn" not in resp.text
    # 回调本身没走完 (openId 为空) 与"自动注册失败"是两种原因, 文案不能混
    assert "登录未完成或已过期" in resp.text


def test_callback_auto_registers_when_unbound(client):
    """未绑定的第三方账号应由 /api/third/register 的同一实现自动注册后继续登录。

    真实扫码回调依赖腾讯, 自动化里拿不到带 openId 的 loginValue, 所以这里只固化
    "不跳 ueloginreturn、且不把回调未完成误报成注册失败" 这条可确定验证的行为;
    完整的"扫码 -> 自动建号 -> 客户端拿到 token"需要人工用手机验一次。
    """
    state = client.device_login_start(LOOPBACK)["data"]["state"]
    third_code = client.third_authorization_url("qq", ueadmin_state=state)["data"]["code"]

    resp = client.third_callback_raw("qq", code="bogus-code", state=third_code)
    assert resp.status_code == 200
    assert "ueloginreturn" not in resp.text
    # 不应出现注册失败的措辞 (openId 为空时压根没走到注册那一步)
    assert "自动注册账号未成功" not in resp.text


def test_callback_without_device_session_keeps_custom_protocol(client):
    """不带设备会话时保持旧行为: 跳 ueloginreturn (PDMS / 旧 ueclient 回归)"""
    third_code = client.third_authorization_url("wechat")["data"]["code"]

    resp = client.third_callback_raw("wechat", code="bogus-code", state=third_code)
    assert resp.status_code == 200
    assert "ueloginreturn://success?state=" in resp.text


def test_login_done_page(client):
    """登录完成页可访问, 且不再出现纯文本 OK"""
    resp = client.device_login_done_page()
    assert resp.status_code == 200
    assert "登录成功" in resp.text

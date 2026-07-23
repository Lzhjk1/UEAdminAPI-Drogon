import uuid
import pytest
from api_client import ApiError
from conftest import fetch_code


def _gen_email(test_domain):
    return f"at_{uuid.uuid4().hex[:8]}@{test_domain}"


def test_anonymous_login_token(client, test_domain):
    email = _gen_email(test_domain)
    code = fetch_code(client, email, "Login")
    body = client.action_token_anonymous("Login", code, email)
    assert body["code"] == 0
    data = body["data"]
    assert data["actionToken"]
    assert data["expiresIn"] == 300


def test_anonymous_register_token(client, test_domain):
    email = _gen_email(test_domain)
    code = fetch_code(client, email, "Register")
    body = client.action_token_anonymous("Register", code, email)
    assert body["code"] == 0
    assert body["data"]["actionToken"]


def test_anonymous_wrong_code(client, test_domain):
    email = _gen_email(test_domain)
    client.send_mfa(email, "Login")
    # 不查询真实码, 直接用一个错误码
    with pytest.raises(ApiError) as ei:
        client.action_token_anonymous("Login", "000000", email)
    assert ei.value.code == -401  # ApiError_InvalidVerifyCode


def test_anonymous_target_mismatch(client, test_domain):
    """ActionToken 绑定 target, 用不同 target 申请应失败(因验证码不存在)"""
    email1 = _gen_email(test_domain)
    email2 = _gen_email(test_domain)
    client.send_mfa(email1, "Login")
    # email2 未发送过验证码, 即使用真实码申请也会因 target 不匹配失败
    code_for_email1 = client.get_latest_code(email1, "Login")["data"]["code"]
    with pytest.raises(ApiError) as ei:
        client.action_token_anonymous("Login", code_for_email1, email2)
    assert ei.value.code == -401  # ApiError_InvalidVerifyCode (email2 无验证码)


def test_anonymous_invalid_mfa_type(client, test_domain):
    """匿名接口只允许 Login/Register/LoginOrRegister/ResetPassword, DeleteUser 应被拒绝"""
    email = _gen_email(test_domain)
    client.send_mfa(email, "Login")
    code = client.get_latest_code(email, "Login")["data"]["code"]
    with pytest.raises(ApiError) as ei:
        client.action_token_anonymous("DeleteUser", code, email)
    assert ei.value.code == -105  # ApiError_InvalidOperation


def test_logged_in_action_token_requires_mfa(fresh_email_account, test_domain):
    """已绑定邮箱的用户申请 ActionToken 必须提供 mfaCode+target"""
    login_client = fresh_email_account.client
    with pytest.raises(ApiError) as ei:
        login_client.action_token("DeleteUser")
    assert ei.value.code == -102  # 需要 mfaCode 和 target


def test_logged_in_action_token_with_wrong_mfa_type(fresh_email_account):
    """请求不被支持的 mfaType 时应返回 -105 ApiError_InvalidOperation"""
    login_client = fresh_email_account.client
    with pytest.raises(ApiError) as ei:
        login_client.action_token("NotAValidType")
    assert ei.value.code == -105  # ApiError_InvalidOperation
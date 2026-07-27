import uuid
import pytest
from api_client import ApiClient, ApiError
from conftest import fetch_code, _delete_account, TestAccount


def _gen_email(test_domain):
    return f"login_{uuid.uuid4().hex[:8]}@{test_domain}"


def test_login_pwd_success(registered_email_account):
    client = registered_email_account.client
    assert client._auth_token, "登录后应持有 token"


def test_login_pwd_wrong(fresh_client, registered_email_account):
    """使用错误密码登录应返回 -301"""
    with pytest.raises(ApiError) as ei:
        fresh_client.login_pwd(registered_email_account.username, "WrongPassword123")
    assert ei.value.code == -301


def test_login_email_code(fresh_client, test_domain):
    """邮箱验证码登录: 动态取码 -> 匿名 ActionToken(Login) -> login_email"""
    email = _gen_email(test_domain)
    # 先注册账号
    reg_code = fetch_code(fresh_client, email, "Register")
    at = fresh_client.action_token_anonymous("Register", reg_code, email)["data"]["actionToken"]
    fresh_client.set_action_token(at)
    fresh_client.register(username=f"u_{uuid.uuid4().hex[:8]}", password="Pwd@12345678", email=email)
    fresh_client.set_action_token(None)

    # 邮箱验证码登录
    login_client = ApiClient(fresh_client.base_url)
    login_code = fetch_code(login_client, email, "Login")
    at = login_client.action_token_anonymous("Login", login_code, email)["data"]["actionToken"]
    body = login_client.login_email(email, at)
    assert body["code"] == 0
    assert body["data"]["token"]

    # 清理: 用登录态删除
    account = TestAccount(body["data"].get("userName") or f"u_{email}", "Pwd@12345678", email=email)
    account.client = login_client
    _delete_account(account)


def test_login_phone_code(fresh_client):
    """手机验证码登录"""
    import random
    phone = "1" + "".join(str(random.randint(0, 9)) for _ in range(10))

    reg_code = fetch_code(fresh_client, phone, "Register")
    at = fresh_client.action_token_anonymous("Register", reg_code, phone)["data"]["actionToken"]
    fresh_client.set_action_token(at)
    fresh_client.register(username=f"u_{uuid.uuid4().hex[:8]}", password="Pwd@12345678", phone=phone)
    fresh_client.set_action_token(None)

    login_client = ApiClient(fresh_client.base_url)
    login_code = fetch_code(login_client, phone, "Login")
    at = login_client.action_token_anonymous("Login", login_code, phone)["data"]["actionToken"]
    body = login_client.login_phone(phone, at)
    assert body["code"] == 0
    assert body["data"]["token"]

    # 清理
    account = TestAccount(body["data"].get("userName") or f"u_{phone}", "Pwd@12345678", phone=phone)
    account.client = login_client
    _delete_account(account)


def test_login_flashtoken_and_verify(registered_email_account):
    """FlashToken 登录与 Token 校验
    顺序: 先验证 FlashToken(尚未被消费) 再用其登录.
    后端验证接口对已使用过的 FlashToken 会返回 invalid,
    因此不能先 login_flashtoken 再 verify_flash_token.
    """
    client = registered_email_account.client
    flash_token = client._flash_token
    assert flash_token, "密码登录应返回 flashToken"

    # 先验证 FlashToken(尚未使用, 应有效)
    vbody = client.verify_flash_token(flash_token)
    assert vbody["code"] == 0
    assert vbody["data"]["tokenType"] == "flashToken"

    # 用 FlashToken 在新 client 上登录
    new_client = ApiClient(client.base_url)
    body = new_client.login_flashtoken(flash_token)
    assert body["code"] == 0
    assert body["data"]["token"]

    # 验证新登录获取的 AuthToken
    abody = new_client.verify_auth_token(new_client._auth_token)
    assert abody["code"] == 0
    assert abody["data"]["tokenType"] == "token"


def test_logout(fresh_client, test_domain):
    """注销: 自行注册临时账号 -> 登录 -> 注销 -> self 失败; 用例内清理账号"""
    email = _gen_email(test_domain)
    username = f"lo_{uuid.uuid4().hex[:8]}"
    password = "Pwd@12345678"

    reg_code = fetch_code(fresh_client, email, "Register")
    at = fresh_client.action_token_anonymous("Register", reg_code, email)["data"]["actionToken"]
    fresh_client.set_action_token(at)
    fresh_client.register(username=username, password=password, email=email)
    fresh_client.set_action_token(None)

    client = ApiClient(fresh_client.base_url)
    client.login_pwd(username, password)
    body = client.logout()
    assert body["code"] == 0
    # 注销后 self 接口应失败
    with pytest.raises(ApiError):
        client.get_self()

    # 清理: token 已注销, _delete_account 会重新登录后删除
    account = TestAccount(username, password, email=email)
    account.client = client
    _delete_account(account)


def test_get_self(registered_email_account):
    client = registered_email_account.client
    body = client.get_self()
    assert body["code"] == 0
    data = body["data"]
    assert data["email"] == registered_email_account.email
    assert data["userName"] == registered_email_account.username
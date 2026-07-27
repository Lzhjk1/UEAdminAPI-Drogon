import uuid
import pytest
from api_client import ApiClient, ApiError
from conftest import fetch_code, _delete_account, TestAccount


def _gen_email(test_domain):
    return f"reg_{uuid.uuid4().hex[:8]}@{test_domain}"


def _gen_phone():
    import random
    return "1" + "".join(str(random.randint(0, 9)) for _ in range(10))


def _quick_register_and_cleanup(client, target, mode):
    """通用工具: 动态取码 -> 匿名 ActionToken -> 快速注册 -> 登录 -> 删除(回收)"""
    code = fetch_code(client, target, "Register")
    at_body = client.action_token_anonymous("Register", code, target)
    action_token = at_body["data"]["actionToken"]
    client.set_action_token(action_token)
    if mode == "phone":
        reg_body = client.register_by_phone(target)
    else:
        reg_body = client.register_by_email(target)
    client.set_action_token(None)
    assert reg_body["code"] == 0, f"快速注册失败: {reg_body}"
    username = reg_body["data"]["username"]
    password = reg_body["data"]["password"]

    # 登录并删除该账号(回收), 复用 _delete_account
    login_client = ApiClient(client.base_url)
    account = TestAccount(username, password,
                          email=target if mode == "email" else None,
                          phone=target if mode == "phone" else None)
    account.login(login_client)
    _delete_account(account)
    return reg_body


def test_register_by_email(client, test_domain):
    email = _gen_email(test_domain)
    body = _quick_register_and_cleanup(client, email, "email")
    assert body["data"]["username"]
    assert body["data"]["password"]


def test_register_by_phone(client):
    phone = _gen_phone()
    body = _quick_register_and_cleanup(client, phone, "phone")
    assert body["data"]["username"]
    assert body["data"]["password"]


def test_general_register_and_check_exist(fresh_email_account, test_domain):
    """通用注册(由 conftest fixture 完成)后, check_exist 应返回 exist=true"""
    client = fresh_email_account.client
    body = client.check_user_exist(fresh_email_account.email)
    assert body["code"] == 0
    assert body["data"]["exist"] is True


def test_check_exist_nonexistent(fresh_email_account, test_domain):
    """已登录客户端查询不存在邮箱应返回 exist=false"""
    client = fresh_email_account.client
    email = _gen_email(test_domain)
    body = client.check_user_exist(email)
    assert body["code"] == 0
    assert body["data"]["exist"] is False


def test_register_missing_args(fresh_client, test_domain):
    """缺少必要参数应返回 -102"""
    email = _gen_email(test_domain)
    code = fetch_code(fresh_client, email, "Register")
    at = fresh_client.action_token_anonymous("Register", code, email)["data"]["actionToken"]
    fresh_client.set_action_token(at)
    with pytest.raises(ApiError) as ei:
        # 不传 username/password 仅有 email
        fresh_client._request("POST", "/api/user/create", json_body={"email": email})
    assert ei.value.code == -102
    fresh_client.set_action_token(None)
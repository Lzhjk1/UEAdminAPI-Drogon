import uuid
import pytest
from api_client import ApiClient, ApiError
from conftest import fetch_code, _delete_account, TestAccount


def _gen_email(test_domain):
    return f"upd_{uuid.uuid4().hex[:8]}@{test_domain}"


def _apply_modify_action_token(client, target):
    """为已绑定 target 的用户申请 ModifyUser ActionToken(动态取码)"""
    code = fetch_code(client, target, "ModifyUser")
    at = client.action_token("ModifyUser", code, target)["data"]["actionToken"]
    client.set_action_token(at)


def test_update_nickname(fresh_email_account):
    client = fresh_email_account.client
    _apply_modify_action_token(client, fresh_email_account.email)
    new_nick = f"nick_{uuid.uuid4().hex[:6]}"
    body = client.update_user(nickname=new_nick)
    assert body["code"] == 0
    client.set_action_token(None)

    self_body = client.get_self()
    assert self_body["data"]["userNick"] == new_nick


def test_update_is_male(fresh_email_account):
    client = fresh_email_account.client
    _apply_modify_action_token(client, fresh_email_account.email)
    body = client.update_user(isMale="false")
    assert body["code"] == 0
    client.set_action_token(None)
    self_body = client.get_self()
    assert self_body["data"]["sex"] == "woman"


def test_update_email(fresh_email_account, test_domain):
    client = fresh_email_account.client
    new_email = _gen_email(test_domain)
    # 申请新邮箱的 EmailBind 验证码
    client.send_mfa(new_email, "EmailBind")
    new_code = client.get_latest_code(new_email, "EmailBind")["data"]["code"]
    # 申请 ModifyUser ActionToken(用当前绑定 email)
    _apply_modify_action_token(client, fresh_email_account.email)
    body = client.update_user(email=new_email, newMfaCode=new_code)
    assert body["code"] == 0
    client.set_action_token(None)

    self_body = client.get_self()
    assert self_body["data"]["email"] == new_email

    # 更新 account.email 以便 fixture teardown 用新 email 删除
    fresh_email_account.email = new_email


def test_update_email_already_bound(fresh_email_account, fresh_client, test_domain):
    """新邮箱已被其他账号绑定时应返回 -304"""
    other_email = _gen_email(test_domain)
    other_username = f"occ_{uuid.uuid4().hex[:6]}"
    other_password = "Pwd@12345678"
    client = fresh_email_account.client

    # 注册占位账号
    reg_code = fetch_code(fresh_client, other_email, "Register")
    at = fresh_client.action_token_anonymous("Register", reg_code, other_email)["data"]["actionToken"]
    fresh_client.set_action_token(at)
    fresh_client.register(username=other_username, password=other_password, email=other_email)
    fresh_client.set_action_token(None)

    # 当前账号尝试改成 other_email
    client.send_mfa(other_email, "EmailBind")
    other_code = client.get_latest_code(other_email, "EmailBind")["data"]["code"]
    _apply_modify_action_token(client, fresh_email_account.email)
    with pytest.raises(ApiError) as ei:
        client.update_user(email=other_email, newMfaCode=other_code)
    assert ei.value.code == -304  # ApiError_EmailAlreadyExists
    client.set_action_token(None)

    # 清理占位账号
    occ_login_client = ApiClient(fresh_client.base_url)
    occ_login_client.login_pwd(other_username, other_password)
    occ_account = TestAccount(other_username, other_password, email=other_email)
    occ_account.client = occ_login_client
    _delete_account(occ_account)


def test_update_unbind_email(fresh_email_account):
    client = fresh_email_account.client
    cur_email = fresh_email_account.email

    # 解绑邮箱需要 Unbind 验证码(发给当前 email)
    unbind_code = fetch_code(client, cur_email, "Unbind")
    _apply_modify_action_token(client, cur_email)
    body = client.update_user(unbindEmail="true", newMfaCode=unbind_code)
    assert body["code"] == 0
    client.set_action_token(None)

    self_body = client.get_self()
    # 解绑后 email 应为空
    email_val = self_body["data"].get("email")
    assert email_val is None or email_val == ""
    # 已解绑后 fixture teardown 不再尝试用 email 申请验证码
    fresh_email_account.email = None


def test_delete_user(fresh_email_account):
    """端到端删除: 复用夹具已登录账号 -> 申请 DeleteUser ActionToken -> 删除 -> 自查失败"""
    client = fresh_email_account.client
    email = fresh_email_account.email

    code = fetch_code(client, email, "DeleteUser")
    at = client.action_token("DeleteUser", code, email)["data"]["actionToken"]
    client.set_action_token(at)
    body = client.delete_user()
    assert body["code"] == 0
    client.set_action_token(None)

    # 删除后旧 token 应失效
    with pytest.raises(ApiError):
        client.get_self()

    # 已删除, 避免 fixture teardown 再次尝试删除
    fresh_email_account.email = None
"""pytest 全局夹具

环境变量(可在运行前 export):
  UEADMINAPI_BASE_URL  服务地址, 默认 http://127.0.0.1:80
  UEADMINAPI_TEST_DOMAIN 测试用邮箱域名, 默认 example.com

验证码采用"发送 -> 调试接口查询"的动态模式, 不依赖固定码;
后端 FixedCode 可留空(随机)或设为固定值, 测试侧均通过 /api/test/mfa/latest 取回。
"""
from __future__ import annotations

import os
import time
import uuid
import pytest

from api_client import ApiClient, ApiError


def _env(name: str, default: str) -> str:
    return os.environ.get(name, default)


@pytest.fixture(scope="session")
def base_url() -> str:
    return _env("UEADMINAPI_BASE_URL", "https://im.uesoft.com")


@pytest.fixture(scope="session")
def test_domain() -> str:
    return _env("UEADMINAPI_TEST_DOMAIN", "example.com")


@pytest.fixture(scope="session")
def client(base_url) -> ApiClient:
    return ApiClient(base_url)


@pytest.fixture(scope="session", autouse=True)
def testmode_enabled(client: ApiClient, test_domain: str):
    """session 级闸门: 验证服务可达且 TestMode 已开启, 否则全部用例无意义"""
    try:
        body = client.ping()
        assert body.get("code") == 0, "服务 ping 不可达"
    except Exception as e:
        pytest.exit(f"服务不可达, 请先启动后端: {e}", returncode=1)
    try:
        target = f"gate_{uuid.uuid4().hex[:8]}@{test_domain}"
        client.send_mfa(target, "Login")
        body = client.get_latest_code(target, "Login")
        assert body.get("code") == 0 and body.get("data", {}).get("code"), \
            "调试接口未返回验证码, 请确认 TestMode.Enable=true"
    except Exception as e:
        pytest.exit(
            f"测试模式未开启或调试接口不可用, 自动化测试无法继续. "
            f"请在 config.json 设置 custom_config.TestMode.Enable=true 并重启服务. 原因: {e}",
            returncode=1,
        )


# ---------- 动态验证码取码工具 ----------
def fetch_code(client: ApiClient, target: str, mfa_type: str) -> str:
    """发送验证码并通过调试接口取回(测试模式专用), 不依赖固定码"""
    client.send_mfa(target, mfa_type)
    body = client.get_latest_code(target, mfa_type)
    code = (body.get("data") or {}).get("code")
    assert code, f"未能从调试接口取回验证码: {body}"
    return code


@pytest.fixture
def fresh_client(base_url) -> ApiClient:
    """每个用例独立 client, 避免相互污染 token 状态"""
    return ApiClient(base_url)


# ---------- 测试账号生成 ----------
class TestAccount:
    __test__ = False  # 告知 pytest 不要把这个类当成测试类收集

    def __init__(self, username: str, password: str, email: str | None = None,
                 phone: str | None = None):
        self.username = username
        self.password = password
        self.email = email
        self.phone = phone
        self.user_id: int | None = None
        self.client: ApiClient | None = None  # 已登录的客户端

    def login(self, client: ApiClient):
        body = client.login_pwd(self.username, self.password)
        self.user_id = (body.get("data") or {}).get("id")
        self.client = client


def _gen_email(test_domain: str) -> str:
    return f"test_{uuid.uuid4().hex[:8]}@{test_domain}"


def _gen_phone() -> str:
    # 1 开头的 11 位号
    suffix = uuid.uuid4().hex[:10]
    suffix = "".join(str(int(c, 16) % 10) for c in suffix)
    return "1" + suffix.zfill(10)[:10]


def _unique_username() -> str:
    return f"test_{uuid.uuid4().hex[:8]}"


def _unique_password() -> str:
    return f"Pwd@{uuid.uuid4().hex[:10]}"


def _delete_account(account: "TestAccount"):
    """回收用户: 若 login_client token 已失效则重新登录; 已绑定 target 需先发 DeleteUser 验证码再申请 ActionToken"""
    login_client = account.client
    try:
        if account.username and account.password and not login_client._auth_token:
            login_client.login_pwd(account.username, account.password)
        if account.email:
            code = fetch_code(login_client, account.email, "DeleteUser")
            at_body = login_client.action_token("DeleteUser", code, account.email)
        elif account.phone:
            code = fetch_code(login_client, account.phone, "DeleteUser")
            at_body = login_client.action_token("DeleteUser", code, account.phone)
        else:
            at_body = login_client.action_token("DeleteUser")
        at = (at_body.get("data") or {}).get("actionToken")
        login_client.set_action_token(at)
        login_client.delete_user()
    except Exception as e:
        # 回收失败不阻断测试, 但打印警告便于排查残留账号
        print(f"[WARN] 账号回收失败 (username={account.username}, email={account.email}, "
              f"phone={account.phone}): {e}")
    finally:
        login_client.set_action_token(None)


# ---------- 注册账号的夹具, 自动办理 ActionToken ----------
@pytest.fixture
def registered_email_account(client: ApiClient, test_domain: str):
    """注册一个邮箱账号并返回 TestAccount; 自动登录; 用例结束自动删除"""
    email = _gen_email(test_domain)
    username = _unique_username()
    password = _unique_password()
    target = email

    # 动态取码 -> 申请匿名 ActionToken (mfaType=Register)
    code = fetch_code(client, target, "Register")
    body = client.action_token_anonymous("Register", code, target)
    action_token = (body.get("data") or {}).get("actionToken")
    assert action_token, f"获取 ActionToken 失败: {body}"

    client.set_action_token(action_token)
    reg_body = client.register(username=username, password=password, email=email)
    assert reg_body.get("code") == 0, f"注册失败: {reg_body}"
    user_id = (reg_body.get("data") or {}).get("userId")
    client.set_action_token(None)

    account = TestAccount(username, password, email=email)
    account.user_id = user_id
    # 用新 client 登录以隔离状态
    login_client = ApiClient(client.base_url)
    account.login(login_client)

    yield account

    # teardown: 删除该用户 (已绑定 email, 需动态取码)
    _delete_account(account)


@pytest.fixture
def fresh_email_account(fresh_client: ApiClient, test_domain: str):
    """同上, 但使用独立 client (用于不希望影响 session 级 client 的用例)"""
    email = _gen_email(test_domain)
    username = _unique_username()
    password = _unique_password()
    target = email

    code = fetch_code(fresh_client, target, "Register")
    body = fresh_client.action_token_anonymous("Register", code, target)
    action_token = (body.get("data") or {}).get("actionToken")
    assert action_token, f"获取 ActionToken 失败: {body}"

    fresh_client.set_action_token(action_token)
    reg_body = fresh_client.register(username=username, password=password, email=email)
    assert reg_body.get("code") == 0, f"注册失败: {reg_body}"
    fresh_client.set_action_token(None)

    # 用全新 client 登录以便清理
    login_client = ApiClient(fresh_client.base_url)
    account = TestAccount(username, password, email=email)
    account.login(login_client)

    yield account

    _delete_account(account)
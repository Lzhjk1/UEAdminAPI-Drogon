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

# ---------- 直连数据库的种子账号（团队 / 计费用例） ----------
# 这些用例依赖一个已存在的团队与指定权限的账号，而注册接口会联动 GitLab，
# 本地/CI 环境未必具备，因此改为直连数据库种入账号。
# 需要设置 UEADMINAPI_TEST_DB_URL（如 postgresql://ueadmin:ueadmin@127.0.0.1:5432/UEAdminAPI）
# 且运行环境有 psql 客户端；未设置时相关用例自动 skip。
import hashlib
import hmac
import subprocess
from types import SimpleNamespace


def _db_url() -> str | None:
    return os.environ.get("UEADMINAPI_TEST_DB_URL")


def _require_db():
    if not _db_url():
        pytest.skip("未设置 UEADMINAPI_TEST_DB_URL，跳过需要直连数据库的用例")


def _sql_lit(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def _psql(sql: str) -> list[str]:
    _require_db()
    proc = subprocess.run(
        ["psql", _db_url(), "-v", "ON_ERROR_STOP=1", "-tA", "-c", sql],
        capture_output=True, text=True,
    )
    if proc.returncode != 0:
        raise RuntimeError(f"psql 执行失败: {proc.stderr.strip()}")
    return [line for line in proc.stdout.splitlines() if line != ""]


def _password_hash(password: str) -> tuple[str, str]:
    """与 AuthService::CreatePasswordHash 一致：64 字节随机盐 + HMAC-SHA512(密码)"""
    salt = os.urandom(64)
    digest = hmac.new(salt, password.encode(), hashlib.sha512).digest()
    return digest.hex(), salt.hex()


def _seed_user(name: str, password: str, privilege: int) -> int:
    pw_hash, pw_salt = _password_hash(password)
    rows = _psql(
        'insert into "user"(name, password_salt, password_hash, privilege, nick_name, '
        "telephone_number, email, is_male, create_at, must_change_pwd) values ("
        f"{_sql_lit(name)}, decode({_sql_lit(pw_salt)}, 'hex'), decode({_sql_lit(pw_hash)}, 'hex'), "
        f"{privilege}, {_sql_lit(name)}, '', {_sql_lit(name + '@example.com')}, true, now(), false) "
        "returning id"
    )
    return int(rows[0])


@pytest.fixture
def seeded_org(test_domain: str):
    """种入「厂商超管 + 团队管理员 + 一个专属团队」，用例结束清理。

    返回 SimpleNamespace:
      team_id / team_name
      admin_name, admin_password, admin_id      (团队管理员, privilege=3)
      vendor_name, vendor_password, vendor_id   (厂商超管, privilege=7)
      member_name, member_password              (留给用例通过接口建号)
    """
    _require_db()
    suffix = uuid.uuid4().hex[:8]
    admin_name = f"pt_admin_{suffix}"
    vendor_name = f"pt_vendor_{suffix}"
    member_name = f"pt_member_{suffix}"
    password = f"Pwd@{suffix}"
    team_name = f"PT-{suffix}"

    admin_id = _seed_user(admin_name, password, 3)
    vendor_id = _seed_user(vendor_name, password, 7)
    team_id = int(_psql(
        f"insert into team(name, owner_user_id, status) values ({_sql_lit(team_name)}, {admin_id}, 1) "
        "returning id"
    )[0])
    _psql(
        "insert into team_member(team_id, user_id, role_id, status) "
        f"select {team_id}, {admin_id}, id, 1 from role where code = 'team_admin'"
    )
    _psql(f"insert into user_balance(user_id) values ({admin_id}), ({vendor_id})")

    org = SimpleNamespace(
        team_id=team_id,
        team_name=team_name,
        admin_name=admin_name,
        admin_password=password,
        admin_id=admin_id,
        vendor_name=vendor_name,
        vendor_password=password,
        vendor_id=vendor_id,
        member_name=member_name,
        member_password=password,
    )
    yield org

    names = ", ".join(_sql_lit(n) for n in (admin_name, vendor_name, member_name))
    user_filter = f'user_id in (select id from "user" where name in ({names}))'
    for sql in (
        f"delete from user_system_role where {user_filter}",
        f"delete from balance_txn where {user_filter}",
        f"delete from payment_order where {user_filter}",
        f"delete from user_balance where {user_filter}",
        f"delete from team_member where {user_filter}",
        f"delete from team_subscription where team_id = {team_id}",
        f"delete from team_feature_auth where team_id = {team_id}",
        f"delete from team where id = {team_id}",
        f'delete from "user" where name in ({names})',
    ):
        try:
            _psql(sql)
        except Exception as e:  # 清理失败不阻断, 但留下线索
            print(f"[WARN] 清理失败: {sql} -> {e}")


@pytest.fixture
def logged_client(base_url):
    """独立 ApiClient 工厂: 用账号密码登录后返回已带 token 的 client"""
    created: list[ApiClient] = []

    def _login(username: str, password: str) -> ApiClient:
        c = ApiClient(base_url)
        c.login_pwd(username, password)
        created.append(c)
        return c

    yield _login

    for c in created:
        try:
            c.logout()
        except Exception:
            pass


@pytest.fixture
def seeded_vendor_org(test_domain: str):
    """种入「厂商团队 + 普通团队 + 个人团队」，用于跨团队管理用例。

    返回 SimpleNamespace:
      vendor_name/password/id     厂商超管（privilege=7，同时是厂商团队 team_admin）
      vendor_team_id/name         team.is_vendor=true
      admin_name/password/id      普通团队 team_admin
      team_id/name                普通团队
      eng_name/password/id        普通团队工程师（只读）
      mover_name/password/id      个人团队（is_personal=true）所有者，供移动用例
      mover_team_id               个人团队
    """
    _require_db()
    suffix = uuid.uuid4().hex[:8]
    password = f"Pwd@{suffix}"
    vendor_name = f"vt_vendor_{suffix}"
    admin_name = f"vt_admin_{suffix}"
    eng_name = f"vt_eng_{suffix}"
    mover_name = f"vt_mover_{suffix}"
    vendor_team_name = f"VT-VENDOR-{suffix}"
    team_name = f"VT-TEAM-{suffix}"
    personal_team_name = f"VT-PERSONAL-{suffix}"

    vendor_id = _seed_user(vendor_name, password, 7)
    admin_id = _seed_user(admin_name, password, 3)
    eng_id = _seed_user(eng_name, password, 3)
    mover_id = _seed_user(mover_name, password, 3)

    vendor_team_id = int(_psql(
        "insert into team(name, owner_user_id, status, is_vendor) values "
        f"({_sql_lit(vendor_team_name)}, {vendor_id}, 1, true) returning id"
    )[0])
    team_id = int(_psql(
        f"insert into team(name, owner_user_id, status) values ({_sql_lit(team_name)}, {admin_id}, 1) "
        "returning id"
    )[0])
    personal_team_id = int(_psql(
        "insert into team(name, owner_user_id, status, is_personal) values "
        f"({_sql_lit(personal_team_name)}, {mover_id}, 1, true) returning id"
    )[0])

    for tid, uid, role_code in (
        (vendor_team_id, vendor_id, "team_admin"),
        (team_id, admin_id, "team_admin"),
        (team_id, eng_id, "engineer"),
        (personal_team_id, mover_id, "team_admin"),
    ):
        _psql(
            "insert into team_member(team_id, user_id, role_id, status) "
            f"select {tid}, {uid}, id, 1 from role where code = {_sql_lit(role_code)}"
        )
        _psql(
            "insert into team_member_role(team_member_id, role_id) "
            "select tm.id, tm.role_id from team_member tm "
            f"where tm.team_id = {tid} and tm.user_id = {uid} on conflict do nothing"
        )
    _psql(
        "insert into user_balance(user_id) values "
        f"({vendor_id}), ({admin_id}), ({eng_id}), ({mover_id}) on conflict do nothing"
    )

    org = SimpleNamespace(
        vendor_name=vendor_name,
        vendor_password=password,
        vendor_id=vendor_id,
        vendor_team_id=vendor_team_id,
        vendor_team_name=vendor_team_name,
        admin_name=admin_name,
        admin_password=password,
        admin_id=admin_id,
        team_id=team_id,
        team_name=team_name,
        eng_name=eng_name,
        eng_password=password,
        eng_id=eng_id,
        mover_name=mover_name,
        mover_password=password,
        mover_id=mover_id,
        mover_team_id=personal_team_id,
    )
    yield org

    names = ", ".join(_sql_lit(n) for n in (vendor_name, admin_name, eng_name, mover_name))
    user_filter = f'user_id in (select id from "user" where name in ({names}))'
    team_filter = ", ".join(str(t) for t in (vendor_team_id, team_id, personal_team_id))
    for sql in (
        f"delete from team_member_role where team_member_id in "
        f"(select id from team_member where {user_filter})",
        f"delete from user_system_role where {user_filter}",
        f"delete from user_balance where {user_filter}",
        f"delete from team_member where {user_filter}",
        f"delete from team_subscription where team_id in ({team_filter})",
        f"delete from team_feature_auth where team_id in ({team_filter})",
        f"delete from team where id in ({team_filter})",
        f'delete from "user" where name in ({names})',
    ):
        try:
            _psql(sql)
        except Exception as e:
            print(f"[WARN] 清理失败: {sql} -> {e}")


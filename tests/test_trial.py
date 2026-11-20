"""新用户试用与订阅到期拦截

依赖直连数据库（UEADMINAPI_TEST_DB_URL）；未设置时整组用例 skip。
"""
from __future__ import annotations

import os
import uuid

import pytest

from api_client import ApiClient, ApiError
from conftest import _delete_account, _psql, _sql_lit, TestAccount, fetch_code


def _set_subscription(team_id: int, end_offset_days: int, *, is_trial: bool, plan: str):
    """按北京时间把团队订阅改成 end_offset_days 天后到期（-1 = 昨天到期）。"""
    _psql(f"delete from team_subscription where team_id = {team_id}")
    _psql(
        "insert into team_subscription(team_id, plan, start_date, end_date, status, is_trial) "
        f"values ({team_id}, {_sql_lit(plan)}, "
        "(now() at time zone 'Asia/Shanghai')::date - 30, "
        f"(now() at time zone 'Asia/Shanghai')::date + {int(end_offset_days)}, 1, "
        f"{'true' if is_trial else 'false'})"
    )


def test_trial_active_login_and_bootstrap(seeded_org, logged_client):
    _set_subscription(seeded_org.team_id, 5, is_trial=True, plan="trial")

    client = logged_client(seeded_org.admin_name, seeded_org.admin_password)
    body = client.bootstrap()
    sub = body["data"]["subscription"]
    assert sub["active"] is True
    assert sub["isTrial"] is True
    assert sub["plan"] == "trial"
    assert sub["remainingDays"] == 5


def test_trial_expired_login_allowed_free(seeded_org, fresh_client):
    """试用到期后仍可登录，客户端按 bootstrap 的订阅状态降级为 Free。"""
    _set_subscription(seeded_org.team_id, -1, is_trial=True, plan="trial")

    body = fresh_client.login_pwd(seeded_org.admin_name, seeded_org.admin_password)
    assert body["code"] == 0
    assert body["data"]["token"]

    boot = fresh_client.bootstrap()
    sub = boot["data"]["subscription"]
    assert sub["active"] is False
    assert sub["isTrial"] is True
    assert sub["remainingDays"] <= 0


def test_paid_subscription_expired_login_allowed_free(seeded_org, fresh_client):
    _set_subscription(seeded_org.team_id, -1, is_trial=False, plan="standard")

    body = fresh_client.login_pwd(seeded_org.admin_name, seeded_org.admin_password)
    assert body["code"] == 0
    assert body["data"]["token"]

    boot = fresh_client.bootstrap()
    sub = boot["data"]["subscription"]
    assert sub["active"] is False
    assert sub["isTrial"] is False
    assert sub["plan"] == "standard"


def test_flash_token_refresh_allowed_after_expiry(seeded_org, base_url):
    """FlashToken 换新 Token 不再被订阅拦截；由客户端按 Free 降级。"""
    _set_subscription(seeded_org.team_id, 5, is_trial=True, plan="trial")
    first = ApiClient(base_url)
    body = first.login_pwd(seeded_org.admin_name, seeded_org.admin_password)
    flash_token = body["data"]["flashToken"]
    assert flash_token

    _set_subscription(seeded_org.team_id, -1, is_trial=True, plan="trial")
    second = ApiClient(base_url)
    refreshed = second.login_flashtoken(flash_token)
    assert refreshed["code"] == 0
    assert refreshed["data"]["token"]

    boot = second.bootstrap()
    assert boot["data"]["subscription"]["active"] is False


def test_legacy_user_without_subscription_can_login(seeded_org, fresh_client):
    _psql(f"delete from team_subscription where team_id = {seeded_org.team_id}")

    body = fresh_client.login_pwd(seeded_org.admin_name, seeded_org.admin_password)
    assert body["code"] == 0
    assert body["data"]["token"]


def test_vendor_team_exempt(seeded_org, fresh_client):
    _set_subscription(seeded_org.team_id, -1, is_trial=True, plan="trial")
    _psql(f"update team set is_vendor = true where id = {seeded_org.team_id}")

    body = fresh_client.login_pwd(seeded_org.admin_name, seeded_org.admin_password)
    assert body["code"] == 0


def test_admin_bit_exempt(seeded_org, fresh_client):
    _set_subscription(seeded_org.team_id, -1, is_trial=True, plan="trial")
    _psql(
        'update "user" set privilege = coalesce(privilege, 0) | 4 '
        f"where id = {seeded_org.admin_id}"
    )

    body = fresh_client.login_pwd(seeded_org.admin_name, seeded_org.admin_password)
    assert body["code"] == 0


def test_super_admin_role_exempt(seeded_org, fresh_client):
    _set_subscription(seeded_org.team_id, -1, is_trial=True, plan="trial")
    _psql(
        "insert into user_system_role(user_id, role_id) "
        f"select {seeded_org.admin_id}, id from role where code = 'super_admin' "
        "on conflict do nothing"
    )

    body = fresh_client.login_pwd(seeded_org.admin_name, seeded_org.admin_password)
    assert body["code"] == 0


def test_register_creates_trial_subscription(fresh_client, test_domain):
    """注册成功后应立即写入 30 天试用并开通云工程/在线更新。"""
    email = f"trial_{uuid.uuid4().hex[:8]}@{test_domain}"
    username = f"trial_{uuid.uuid4().hex[:8]}"
    password = f"Pwd@{uuid.uuid4().hex[:10]}"

    code = fetch_code(fresh_client, email, "Register")
    action_token = fresh_client.action_token_anonymous("Register", code, email)["data"]["actionToken"]
    fresh_client.set_action_token(action_token)
    try:
        reg_body = fresh_client.register(username=username, password=password, email=email)
    except ApiError as e:
        if e.code in (-601, -603, -604):
            pytest.skip("环境未配置 GitLab，跳过注册链路用例")
        raise
    finally:
        fresh_client.set_action_token(None)

    user_id = int(reg_body["data"]["userId"])
    login_client = ApiClient(fresh_client.base_url)
    account = TestAccount(username, password, email=email)
    account.login(login_client)

    team_id = 0
    try:
        team_rows = _psql(f"select team_id from team_member where user_id = {user_id}")
        assert team_rows, "注册后应自动加入个人团队"
        team_id = int(team_rows[0])

        rows = _psql(
            "select plan, is_trial, status, end_date - start_date as days, "
            "       (end_date >= (now() at time zone 'Asia/Shanghai')::date) as active "
            f"from team_subscription where team_id = {team_id} order by end_date desc limit 1"
        )
        assert rows, "注册后应自动写入试用订阅"
        plan, is_trial, status, days, active = rows[0].split("|")
        assert plan == "trial"
        assert is_trial == "t"
        assert status == "1"
        assert int(days) == int(os.environ.get("UEADMINAPI_TRIAL_DAYS", "30")) - 1
        assert active == "t"

        features = _psql(
            "select feature_code from team_feature_auth "
            f"where team_id = {team_id} and enabled order by feature_code"
        )
        assert features == ["cloud_project", "online_update"]
    finally:
        # 先清团队相关行，避免用户删除被外键挡住
        if team_id:
            for sql in (
                f"delete from team_subscription where team_id = {team_id}",
                f"delete from team_feature_auth where team_id = {team_id}",
                "delete from team_member_role where team_member_id in "
                f"(select id from team_member where team_id = {team_id})",
                f"delete from team_member where team_id = {team_id}",
                f"delete from team where id = {team_id}",
                f"delete from user_balance where user_id = {user_id}",
            ):
                try:
                    _psql(sql)
                except Exception:
                    pass
        _delete_account(account)

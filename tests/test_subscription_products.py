"""订阅产品化（PDMS / UEAICAD）回归。

覆盖：
  - UEAICAD 有效 ⇒ PDMS 有效（implied）
  - PDMS-only：不获得 AI 套餐，subscription 视图为空
  - UEAICAD 到期 + PDMS 有效：subscription 仍是原 AI 套餐且 active=false
  - 零订阅记录：双 legacy（存量放行）
  - trial 含 PDMS
  - 厂商超管：双 vendor / max
  - 管理接口 product 参数（默认 / 显式 / 非法 -820）
  - 席位：PDMS-only 与“到期 AI + 有效 PDMS”不限；纯到期 AI 仍 1 席

依赖 seeded_org（直连数据库种入团队与管理员）与 psql；未设置
UEADMINAPI_TEST_DB_URL 时整组 skip。
"""
from __future__ import annotations

import datetime
import uuid

import pytest

from api_client import ApiClient
from conftest import _psql, _sql_lit

MEMBER_PASSWORD = "Pwd@123456"
NAME_PREFIX = "prodtest"
TODAY = "(now() at time zone 'Asia/Shanghai')::date"


def _set_subs(team_id: int, rows: list[tuple[str, str, int, bool]]):
    """rows: (product, plan, end_offset_days, is_trial)"""
    _psql(f"delete from team_subscription where team_id = {team_id}")
    for product, plan, offset, is_trial in rows:
        _psql(
            "insert into team_subscription(team_id, product, plan, start_date, end_date, status, is_trial) "
            f"values ({team_id}, {_sql_lit(product)}, {_sql_lit(plan)}, {TODAY} - 30, "
            f"{TODAY} + ({int(offset)}), 1, {'true' if is_trial else 'false'})"
        )


def _data(body: dict) -> dict:
    assert body.get("code") == 0, f"接口失败: {body}"
    return body.get("data") or {}


def _bootstrap(client: ApiClient) -> dict:
    return _data(client.bootstrap())


def _create_member(client: ApiClient, name: str, check: bool = True):
    return client._request(
        "POST", "/api/team/member",
        json_body={"userName": name, "password": MEMBER_PASSWORD,
                   "roleCodes": ["engineer"], "nickName": name},
        check=check,
    )


def _cleanup_created_members():
    users = f"""select id from "user" where name like '{NAME_PREFIX}%'"""
    try:
        _psql("delete from team_member_role where team_member_id in "
              f"(select id from team_member where user_id in ({users}))")
        _psql(f"delete from team_member where user_id in ({users})")
        _psql("delete from team_subscription where team_id in "
              f"(select id from team where owner_user_id in ({users}))")
        _psql("delete from team_feature_auth where team_id in "
              f"(select id from team where owner_user_id in ({users}))")
        _psql(f"delete from team where owner_user_id in ({users})")
        _psql(f"delete from user_balance where user_id in ({users})")
        _psql(f"""delete from "user" where name like '{NAME_PREFIX}%'""")
    except Exception as e:  # 清理失败不阻断用例结论
        print(f"[WARN] 产品订阅用例清理失败: {e}")


@pytest.fixture
def cleanup_members():
    yield
    _cleanup_created_members()


def test_ueaicad_without_pdms_record(seeded_org, logged_client):
    _set_subs(seeded_org.team_id, [("ueaicad", "pro", 365, False)])
    data = _bootstrap(logged_client(seeded_org.admin_name, seeded_org.admin_password))

    assert data["subscription"]["active"] is True
    assert data["subscription"]["plan"] == "pro"
    assert data["hasSubscriptionRecord"] is True
    assert data["products"]["ueaicad"]["active"] is True
    assert data["products"]["ueaicad"]["source"] == "subscription"
    assert data["products"]["pdms"]["active"] is False
    assert data["products"]["pdms"]["source"] == "none"


def test_pdms_only_has_no_ai_subscription(seeded_org, logged_client):
    _set_subs(seeded_org.team_id, [("pdms", "network", 365, False)])
    data = _bootstrap(logged_client(seeded_org.admin_name, seeded_org.admin_password))

    # PDMS 行不得混入 subscription（AI 套餐视图）
    assert data["subscription"]["active"] is False
    assert data["subscription"]["plan"] == ""
    assert data["hasSubscriptionRecord"] is True
    assert data["products"]["pdms"]["active"] is True
    assert data["products"]["pdms"]["source"] == "subscription"
    assert data["products"]["ueaicad"]["active"] is False
    assert data["products"]["ueaicad"]["source"] == "none"


def test_expired_ueaicad_keeps_pdms(seeded_org, logged_client):
    _set_subs(seeded_org.team_id, [
        ("ueaicad", "pro", -1, False),
        ("pdms", "network", 365, False),
    ])
    data = _bootstrap(logged_client(seeded_org.admin_name, seeded_org.admin_password))

    assert data["subscription"]["active"] is False
    assert data["subscription"]["plan"] == "pro"
    assert data["products"]["ueaicad"]["active"] is False
    assert data["products"]["pdms"]["active"] is True
    assert data["products"]["pdms"]["source"] == "subscription"


def test_legacy_without_records(seeded_org, logged_client):
    _set_subs(seeded_org.team_id, [])
    data = _bootstrap(logged_client(seeded_org.admin_name, seeded_org.admin_password))

    assert data["hasSubscriptionRecord"] is False
    assert data["subscription"]["startDate"] == ""
    assert data["products"]["pdms"] == {
        "active": True, "source": "legacy", "startDate": "", "endDate": "", "remainingDays": 0}
    assert data["products"]["ueaicad"]["active"] is True
    assert data["products"]["ueaicad"]["source"] == "legacy"


def test_trial_with_both_records(seeded_org, logged_client):
    _set_subs(seeded_org.team_id, [
        ("ueaicad", "trial", 5, True),
        ("pdms", "trial", 5, True),
    ])
    data = _bootstrap(logged_client(seeded_org.admin_name, seeded_org.admin_password))

    assert data["subscription"]["isTrial"] is True
    assert data["products"]["pdms"]["active"] is True
    assert data["products"]["pdms"]["source"] == "subscription"


def test_trial_without_pdms_record(seeded_org, logged_client):
    _set_subs(seeded_org.team_id, [("ueaicad", "trial", 5, True)])
    data = _bootstrap(logged_client(seeded_org.admin_name, seeded_org.admin_password))

    assert data["products"]["pdms"]["active"] is False
    assert data["products"]["pdms"]["source"] == "none"


def test_vendor_super_admin_gets_both_products(seeded_org, logged_client):
    _psql(f"update team set is_vendor = true where id = {seeded_org.team_id}")
    _psql(
        "insert into team_member(team_id, user_id, role_id, status) "
        f"select {seeded_org.team_id}, {seeded_org.vendor_id}, id, 1 "
        "from role where code = 'team_admin'"
    )
    data = _bootstrap(logged_client(seeded_org.vendor_name, seeded_org.vendor_password))

    assert data["vendorMax"] is True
    assert data["products"]["pdms"]["active"] is True
    assert data["products"]["pdms"]["source"] == "vendor"
    assert data["products"]["ueaicad"]["active"] is True
    assert data["products"]["ueaicad"]["source"] == "vendor"
    assert data["products"]["ueaicad"]["plan"] == "max"


def test_admin_subscription_product_param(seeded_org, logged_client):
    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)
    today = datetime.date.today()
    start = today.isoformat()
    end = (today + datetime.timedelta(days=365)).isoformat()

    default = _data(vendor._request(
        "PUT", f"/api/admin/teams/{seeded_org.team_id}/subscription",
        json_body={"startDate": start, "endDate": end, "plan": "plus"}))
    assert default["product"] == "ueaicad"

    _set_subs(seeded_org.team_id, [])
    explicit = _data(vendor._request(
        "PUT", f"/api/admin/teams/{seeded_org.team_id}/subscription",
        json_body={"startDate": start, "endDate": end, "plan": "network", "product": "pdms"}))
    assert explicit["product"] == "pdms"

    invalid = vendor._request(
        "PUT", f"/api/admin/teams/{seeded_org.team_id}/subscription",
        json_body={"startDate": start, "endDate": end, "product": "bogus"}, check=False)
    assert invalid["code"] == -820, invalid

    teams = _data(vendor._request("GET", "/api/admin/teams"))["teams"]
    row = next(t for t in teams if t["id"] == seeded_org.team_id)
    assert row["hasSubscriptionRecord"] is True
    assert row["pdmsActive"] is True

    detail = _data(vendor._request("GET", f"/api/admin/teams/{seeded_org.team_id}"))
    assert detail["products"]["pdms"]["source"] == "subscription"
    assert detail["products"]["ueaicad"]["active"] is False


def test_admin_subscription_include_pdms_writes_both(seeded_org, logged_client):
    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)
    _set_subs(seeded_org.team_id, [])
    today = datetime.date.today()
    start = today.isoformat()
    end = (today + datetime.timedelta(days=365)).isoformat()

    data = _data(vendor._request(
        "PUT", f"/api/admin/teams/{seeded_org.team_id}/subscription",
        json_body={"startDate": start, "endDate": end, "plan": "plus", "includePdms": True}))
    assert data["product"] == "ueaicad"
    assert data["includePdms"] is True

    detail = _data(vendor._request("GET", f"/api/admin/teams/{seeded_org.team_id}"))
    assert detail["products"]["ueaicad"]["active"] is True
    assert detail["products"]["pdms"]["active"] is True
    assert detail["products"]["pdms"]["source"] == "subscription"


def test_pdms_only_seats_unlimited(seeded_org, logged_client, cleanup_members):
    _set_subs(seeded_org.team_id, [("pdms", "network", 365, False)])
    admin = logged_client(seeded_org.admin_name, seeded_org.admin_password)
    suffix = uuid.uuid4().hex[:6]

    # 管理员 + 4 人 = 5，超过 Plus 的 4 席；PDMS-only 不应受 AI 套餐席位限制
    for i in range(1, 5):
        _data(_create_member(admin, f"{NAME_PREFIX}{suffix}_{i}"))

    # 切成“到期 AI + 有效 PDMS”：仍不限
    _set_subs(seeded_org.team_id, [
        ("ueaicad", "plus", -1, False),
        ("pdms", "network", 365, False),
    ])
    _data(_create_member(admin, f"{NAME_PREFIX}{suffix}_5"))

    # 切成“只有到期 AI”：回到 1 席
    _set_subs(seeded_org.team_id, [("ueaicad", "plus", -1, False)])
    denied = _create_member(admin, f"{NAME_PREFIX}{suffix}_6", check=False)
    assert denied["code"] != 0, denied
    assert "席位已满" in denied["msg"], denied

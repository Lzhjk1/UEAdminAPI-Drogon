"""团队 / 付费 / 授权 / bootstrap 回归用例

依赖 conftest 的 seeded_org 夹具（直连数据库种入账号）。
运行前设置：
  UEADMINAPI_BASE_URL=http://127.0.0.1:6666
  UEADMINAPI_TEST_DB_URL=postgresql://ueadmin:ueadmin@127.0.0.1:5432/UEAdminAPI
"""
from __future__ import annotations

import datetime

import pytest

from api_client import ApiClient, ApiError


def _bootstrap(client: ApiClient) -> dict:
    return client.bootstrap()["data"]


def test_bootstrap_of_new_team_admin(seeded_org, logged_client):
    client = logged_client(seeded_org.admin_name, seeded_org.admin_password)
    data = _bootstrap(client)

    assert data["userId"] == seeded_org.admin_id
    assert data["team"]["id"] == seeded_org.team_id
    assert data["team"]["name"] == seeded_org.team_name
    assert data["team"]["roleCode"] == "team_admin"
    assert data["balanceCents"] == 0
    assert data["subscription"]["active"] is False
    assert data["subscription"]["endDate"] == ""
    assert data["featureAuth"]["cloud_project"] is False
    assert data["featureAuth"]["online_update"] is False
    assert data["mustChangePwd"] is False


def test_team_members_contains_admin(seeded_org, logged_client):
    client = logged_client(seeded_org.admin_name, seeded_org.admin_password)
    data = client.team_info()["data"]

    assert data["team"]["id"] == seeded_org.team_id
    assert data["team"]["roleCode"] == "team_admin"
    assert len(data["members"]) == 1
    admin = data["members"][0]
    assert admin["userName"] == seeded_org.admin_name
    assert admin["roleCode"] == "team_admin"
    assert admin["status"] == 1


def test_create_member_and_first_login_requires_change(seeded_org, logged_client, base_url):
    admin = logged_client(seeded_org.admin_name, seeded_org.admin_password)
    body = admin.create_team_member(seeded_org.member_name, seeded_org.member_password,
                                    "engineer", "工程师一")
    assert body["code"] == 0
    assert body["data"]["userId"] > 0

    member = ApiClient(base_url)
    member.login_pwd(seeded_org.member_name, seeded_org.member_password)
    data = _bootstrap(member)

    assert data["mustChangePwd"] is True
    assert data["team"]["id"] == seeded_org.team_id
    assert data["team"]["roleCode"] == "engineer"
    assert data["balanceCents"] == 0

    assert len(admin.team_info()["data"]["members"]) == 2


def test_create_member_duplicate_and_invalid_role(seeded_org, logged_client):
    admin = logged_client(seeded_org.admin_name, seeded_org.admin_password)
    admin.create_team_member(seeded_org.member_name, seeded_org.member_password, "engineer")

    with pytest.raises(ApiError) as dup:
        admin.create_team_member(seeded_org.member_name, seeded_org.member_password, "engineer")
    assert dup.value.code == -303  # 用户名已存在

    with pytest.raises(ApiError) as bad_role:
        admin.create_team_member(seeded_org.member_name + "_x", seeded_org.member_password, "team_admin")
    assert bad_role.value.code == -805  # 不允许分配团队管理员


def test_member_cannot_manage_team_or_admin(seeded_org, logged_client, base_url):
    admin = logged_client(seeded_org.admin_name, seeded_org.admin_password)
    admin.create_team_member(seeded_org.member_name, seeded_org.member_password, "engineer")

    member = ApiClient(base_url)
    member.login_pwd(seeded_org.member_name, seeded_org.member_password)

    with pytest.raises(ApiError) as create_denied:
        member.create_team_member(seeded_org.member_name + "_y", seeded_org.member_password, "engineer")
    assert create_denied.value.code == -803  # 非团队管理员

    with pytest.raises(ApiError) as admin_denied:
        member.admin_list_teams()
    assert admin_denied.value.code == -807  # 需要管理员权限


def test_admin_sets_subscription_and_feature_auth(seeded_org, logged_client):
    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)

    teams = vendor.admin_list_teams()["data"]["teams"]
    assert any(t["id"] == seeded_org.team_id for t in teams)

    today = datetime.date.today()
    end = today + datetime.timedelta(days=365)
    vendor.admin_set_subscription(seeded_org.team_id, today.isoformat(), end.isoformat(), "standard")
    vendor.admin_set_feature_auth(seeded_org.team_id, "cloud_project", True)

    admin = logged_client(seeded_org.admin_name, seeded_org.admin_password)
    data = _bootstrap(admin)
    assert data["subscription"]["active"] is True
    assert data["subscription"]["startDate"] == today.isoformat()
    assert data["subscription"]["endDate"] == end.isoformat()
    assert data["featureAuth"]["cloud_project"] is True
    assert data["featureAuth"]["online_update"] is False

    with pytest.raises(ApiError) as bad_date:
        vendor.admin_set_subscription(seeded_org.team_id, "2026-13-01", end.isoformat())
    assert bad_date.value.code == -806  # 付费起止日期不合法

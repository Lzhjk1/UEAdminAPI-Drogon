"""跨团队团队管理（/api/vendor/*）与团队改名 / 移除成员回归用例

依赖 conftest 的 seeded_vendor_org 夹具（直连数据库种入团队与账号）。
运行前设置：
  UEADMINAPI_BASE_URL=http://127.0.0.1:6666
  UEADMINAPI_TEST_DB_URL=postgresql://ueadmin:ueadmin@127.0.0.1:5432/UEAdminAPI
"""
from __future__ import annotations

from api_client import ApiClient


def _ok(body: dict) -> dict:
    assert body.get("code") == 0, f"接口失败: {body}"
    return body.get("data") or {}


def test_vendor_lists_all_teams(seeded_vendor_org, logged_client):
    client = logged_client(seeded_vendor_org.vendor_name, seeded_vendor_org.vendor_password)
    data = _ok(client._request("GET", "/api/vendor/teams"))

    names = [team["name"] for team in data["teams"]]
    assert seeded_vendor_org.team_name in names
    assert seeded_vendor_org.vendor_team_name in names
    assert data["ownTeamId"] == seeded_vendor_org.vendor_team_id


def test_normal_team_admin_denied(seeded_vendor_org, logged_client):
    client = logged_client(seeded_vendor_org.admin_name, seeded_vendor_org.admin_password)
    body = client._request("GET", "/api/vendor/teams", check=False)
    assert body["code"] == -807


def test_engineer_is_read_only(seeded_vendor_org, logged_client):
    client = logged_client(seeded_vendor_org.eng_name, seeded_vendor_org.eng_password)
    body = client._request(
        "POST", "/api/team/member",
        json_body={"userName": "vt_denied", "password": "Pwd@123456", "roleCodes": ["engineer"]},
        check=False,
    )
    assert body["code"] == -803


def test_move_member_in_then_remove(seeded_vendor_org, logged_client, base_url):
    vendor = logged_client(seeded_vendor_org.vendor_name, seeded_vendor_org.vendor_password)

    moved = _ok(vendor._request(
        "POST", f"/api/vendor/teams/{seeded_vendor_org.team_id}/members/move-in",
        json_body={"userIds": [seeded_vendor_org.mover_id], "roleCodes": ["engineer"]},
    ))
    assert moved["moved"] == 1
    assert moved["failed"] == 0

    mover = ApiClient(base_url)
    mover.login_pwd(seeded_vendor_org.mover_name, seeded_vendor_org.mover_password)
    info = _ok(mover._request("GET", "/api/team"))
    assert info["team"]["id"] == seeded_vendor_org.team_id
    assert info["team"]["roleCode"] == "engineer"

    removed = _ok(vendor._request(
        "DELETE",
        f"/api/vendor/teams/{seeded_vendor_org.team_id}/members/{seeded_vendor_org.mover_id}",
    ))
    assert removed["teamId"] == seeded_vendor_org.mover_team_id
    assert removed["teamName"].startswith("VT-PERSONAL-")


def test_personal_team_cannot_accept_members(seeded_vendor_org, logged_client):
    vendor = logged_client(seeded_vendor_org.vendor_name, seeded_vendor_org.vendor_password)
    body = vendor._request(
        "POST", f"/api/vendor/teams/{seeded_vendor_org.mover_team_id}/members/move-in",
        json_body={"userIds": [seeded_vendor_org.eng_id], "roleCodes": ["engineer"]},
        check=False,
    )
    assert body["code"] == -105


def test_team_owner_cannot_be_removed(seeded_vendor_org, logged_client):
    vendor = logged_client(seeded_vendor_org.vendor_name, seeded_vendor_org.vendor_password)
    body = vendor._request(
        "DELETE",
        f"/api/vendor/teams/{seeded_vendor_org.team_id}/members/{seeded_vendor_org.admin_id}",
        check=False,
    )
    assert body["code"] == -818


def test_rename_own_team_and_engineer_denied(seeded_vendor_org, logged_client):
    admin = logged_client(seeded_vendor_org.admin_name, seeded_vendor_org.admin_password)
    new_name = seeded_vendor_org.team_name + "-R"
    data = _ok(admin._request("PATCH", "/api/team", json_body={"name": new_name}))
    assert data["name"] == new_name
    assert _ok(admin._request("GET", "/api/team"))["team"]["name"] == new_name

    eng = logged_client(seeded_vendor_org.eng_name, seeded_vendor_org.eng_password)
    body = eng._request("PATCH", "/api/team", json_body={"name": new_name + "-X"}, check=False)
    assert body["code"] == -803

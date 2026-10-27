"""超级管理员（厂商系统角色）回归用例

覆盖：
  - super_admin 是系统角色（scope = 1），出现在角色列表且不允许删除；
  - 厂商（Admin 位）可以把 super_admin 授予或回收给任意账号；
  - 拿到角色的账号可以跨团队查看/修改，但不能再次扩散角色；
  - 团队创建者不能被停用或降级；
  - 团队停用后云端授权、在线更新检查、团队建号都被拦住。

依赖 seeded_org 夹具（直连数据库种入账号）；运行方式见 test_team_bootstrap.py 顶部说明。
"""
from __future__ import annotations

import datetime
import uuid
from types import SimpleNamespace

import pytest

from api_client import ApiError
from conftest import _psql, _sql_lit


@pytest.fixture
def other_team(seeded_org):
    """第二个团队（暂不挂成员），用来证明「跨团队」而不是只看自己团队"""
    name = f"OT-{uuid.uuid4().hex[:8]}"
    team_id = int(_psql(
        "insert into team(name, owner_user_id, status) values "
        f"({_sql_lit(name)}, {seeded_org.vendor_id}, 1) returning id"
    )[0])
    yield SimpleNamespace(team_id=team_id, name=name)

    _psql(f"delete from team_member where team_id = {team_id}")
    _psql(f"delete from team_subscription where team_id = {team_id}")
    _psql(f"delete from team_feature_auth where team_id = {team_id}")
    _psql(f"delete from team where id = {team_id}")


def _vendor(seeded_org, logged_client):
    return logged_client(seeded_org.vendor_name, seeded_org.vendor_password)


def _team_admin(seeded_org, logged_client):
    return logged_client(seeded_org.admin_name, seeded_org.admin_password)


def test_super_admin_is_protected_system_role(seeded_org, logged_client):
    vendor = _vendor(seeded_org, logged_client)

    roles = {r["code"]: r for r in vendor.admin_list_roles()["data"]["roles"]}
    assert "super_admin" in roles
    role = roles["super_admin"]
    assert role["scope"] == 1
    assert role["builtin"] is True
    assert len(role["permissionIds"]) >= 4

    with pytest.raises(ApiError) as protected:
        vendor.admin_delete_role(role["id"])
    assert protected.value.code == -816


def test_vendor_reads_other_team(seeded_org, other_team, logged_client):
    vendor = _vendor(seeded_org, logged_client)

    data = vendor.admin_get_team(other_team.team_id)["data"]
    assert data["team"]["id"] == other_team.team_id
    assert data["team"]["name"] == other_team.name
    assert data["team"]["ownerUserId"] == seeded_org.vendor_id
    assert data["team"]["ownerName"] == seeded_org.vendor_name
    assert data["team"]["status"] == 1
    assert data["memberCount"] == 0
    assert data["members"] == []
    assert data["subscription"]["active"] is False
    assert data["featureAuth"]["cloud_project"] is False
    assert data["featureAuth"]["online_update"] is False

    with pytest.raises(ApiError) as missing:
        vendor.admin_get_team(99999999)
    assert missing.value.code == -801


def test_vendor_renames_and_disables_team(seeded_org, other_team, logged_client):
    vendor = _vendor(seeded_org, logged_client)
    new_name = f"OT-{uuid.uuid4().hex[:8]}"

    vendor.admin_update_team(other_team.team_id, name=new_name, status=2)
    team = vendor.admin_get_team(other_team.team_id)["data"]["team"]
    assert team["name"] == new_name
    assert team["status"] == 2

    # 团队名全局唯一
    with pytest.raises(ApiError) as dup:
        vendor.admin_update_team(other_team.team_id, name=seeded_org.team_name)
    assert dup.value.code == -815

    with pytest.raises(ApiError) as bad_status:
        vendor.admin_update_team(other_team.team_id, status=9)
    assert bad_status.value.code == -105


def test_vendor_changes_member_role_and_status(seeded_org, logged_client):
    vendor = _vendor(seeded_org, logged_client)
    admin = _team_admin(seeded_org, logged_client)

    body = admin.create_team_member(seeded_org.member_name, seeded_org.member_password,
                                    "engineer", "工程师一")
    member_id = body["data"]["userId"]

    data = vendor.admin_get_team(seeded_org.team_id)["data"]
    assert any(m["userId"] == member_id and m["roleCode"] == "engineer" for m in data["members"])

    updated = vendor.admin_update_member(seeded_org.team_id, member_id,
                                          role_code="pm", status=2)["data"]["member"]
    assert updated["roleCode"] == "pm"
    assert updated["status"] == 2

    # 系统角色不能挂到团队成员位
    with pytest.raises(ApiError) as system_role:
        vendor.admin_update_member(seeded_org.team_id, member_id, role_code="super_admin")
    assert system_role.value.code == -805

    # 账号不在该团队
    with pytest.raises(ApiError) as outsider:
        vendor.admin_update_member(seeded_org.team_id, seeded_org.vendor_id, status=2)
    assert outsider.value.code == -802


def test_team_owner_cannot_be_disabled_or_demoted(seeded_org, logged_client):
    vendor = _vendor(seeded_org, logged_client)

    with pytest.raises(ApiError) as disabled:
        vendor.admin_update_member(seeded_org.team_id, seeded_org.admin_id, status=2)
    assert disabled.value.code == -818

    with pytest.raises(ApiError) as demoted:
        vendor.admin_update_member(seeded_org.team_id, seeded_org.admin_id, role_code="engineer")
    assert demoted.value.code == -818


def test_role_grant_takes_effect_and_cannot_spread(seeded_org, other_team, logged_client):
    vendor = _vendor(seeded_org, logged_client)
    admin = _team_admin(seeded_org, logged_client)

    # 授予前：普通团队管理员访问厂商接口被拒
    with pytest.raises(ApiError) as denied:
        admin.admin_get_team(other_team.team_id)
    assert denied.value.code == -807

    vendor.admin_set_system_role(seeded_org.admin_id, "super_admin", True)
    assert admin.bootstrap()["data"]["isSuperAdmin"] is True
    assert admin.admin_get_team(other_team.team_id)["data"]["team"]["id"] == other_team.team_id

    # 角色持有者不能把角色扩散出去，授予权仍然只认 Admin 位
    with pytest.raises(ApiError) as escalate:
        admin.admin_set_system_role(seeded_org.vendor_id, "super_admin", True)
    assert escalate.value.code == -807

    # 回收后立即失去后台权限
    vendor.admin_set_system_role(seeded_org.admin_id, "super_admin", False)
    assert admin.bootstrap()["data"]["isSuperAdmin"] is False
    with pytest.raises(ApiError) as revoked:
        admin.admin_get_team(other_team.team_id)
    assert revoked.value.code == -807


def test_user_search_shows_team_and_super_admin_flag(seeded_org, logged_client):
    vendor = _vendor(seeded_org, logged_client)

    users = vendor.admin_list_users(keyword=seeded_org.admin_name)["data"]["users"]
    admin = next(u for u in users if u["userId"] == seeded_org.admin_id)
    assert admin["teamId"] == seeded_org.team_id
    assert admin["roleCode"] == "team_admin"
    assert admin["isSuperAdmin"] is False

    vendor.admin_set_system_role(seeded_org.admin_id, "super_admin", True)
    users = vendor.admin_list_users(keyword=seeded_org.admin_name)["data"]["users"]
    admin = next(u for u in users if u["userId"] == seeded_org.admin_id)
    assert admin["isSuperAdmin"] is True


def test_disabled_team_blocks_cloud_update_and_member_creation(seeded_org, logged_client):
    vendor = _vendor(seeded_org, logged_client)
    admin = _team_admin(seeded_org, logged_client)

    today = datetime.date.today()
    vendor.admin_set_subscription(seeded_org.team_id, str(today),
                                  str(today + datetime.timedelta(days=30)))
    vendor.admin_set_feature_auth(seeded_org.team_id, "cloud_project", True)
    vendor.admin_set_feature_auth(seeded_org.team_id, "online_update", True)

    data = admin.bootstrap()["data"]
    assert data["featureAuth"]["cloud_project"] is True
    assert data["featureAuth"]["online_update"] is True

    vendor.admin_update_team(seeded_org.team_id, status=2)

    data = admin.bootstrap()["data"]
    assert data["team"]["status"] == 2
    assert data["featureAuth"]["cloud_project"] is False
    assert data["featureAuth"]["online_update"] is False

    check = admin.update_check("0.0.1")["data"]
    assert check["reason"] == "team_disabled"
    assert check["hasUpdate"] is False

    with pytest.raises(ApiError) as blocked:
        admin.create_team_member(seeded_org.member_name, seeded_org.member_password, "engineer")
    assert blocked.value.code == -105

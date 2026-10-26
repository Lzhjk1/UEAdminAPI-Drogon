"""厂商后台 RBAC（角色 / 权限 / 权限组）回归用例

依赖 seeded_org 夹具（直连数据库种入账号）；运行方式见 test_team_bootstrap.py 顶部说明。
"""
from __future__ import annotations

import uuid

import pytest

from api_client import ApiError


def _suffix() -> str:
    return uuid.uuid4().hex[:8]


def _permission_id_by_code(client, code: str) -> int:
    for item in client.admin_list_permissions()["data"]["permissions"]:
        if item["code"] == code:
            return item["id"]
    raise AssertionError(f"未找到权限 {code}")


def _role_by_code(client, code: str) -> dict:
    for item in client.admin_list_roles()["data"]["roles"]:
        if item["code"] == code:
            return item
    raise AssertionError(f"未找到角色 {code}")


def test_builtin_rbac_seed_data(seeded_org, logged_client):
    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)

    permissions = vendor.admin_list_permissions()["data"]["permissions"]
    codes = {p["code"] for p in permissions}
    assert {"cloud_project", "online_update", "team_manage", "billing_view"} <= codes

    roles = {r["code"]: r for r in vendor.admin_list_roles()["data"]["roles"]}
    assert {"team_admin", "engineer", "pm"} <= set(roles)
    assert roles["team_admin"]["builtin"] is True
    assert len(roles["team_admin"]["permissionIds"]) >= 4

    groups = vendor.admin_list_permission_groups()["data"]["groups"]
    assert any(g["name"] == "基础权限组" for g in groups)


def test_permission_crud(seeded_org, logged_client):
    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)
    code = f"perm_{_suffix()}"

    created = vendor.admin_create_permission(code, "test_module", "read")["data"]["permission"]
    assert created["code"] == code

    assert any(p["code"] == code for p in vendor.admin_list_permissions()["data"]["permissions"])

    with pytest.raises(ApiError) as dup:
        vendor.admin_create_permission(code, "test_module", "read")
    assert dup.value.code == -815

    updated = vendor.admin_update_permission(created["id"], "test_module2", "")["data"]["permission"]
    assert updated["module"] == "test_module2"
    assert updated["action"] == "read"

    vendor.admin_delete_permission(created["id"])
    assert not any(p["code"] == code for p in vendor.admin_list_permissions()["data"]["permissions"])


def test_permission_in_use_cannot_be_deleted(seeded_org, logged_client):
    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)
    perm_id = _permission_id_by_code(vendor, "cloud_project")

    with pytest.raises(ApiError) as in_use:
        vendor.admin_delete_permission(perm_id)
    assert in_use.value.code == -814


def test_role_crud_and_permission_assignment(seeded_org, logged_client):
    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)
    code = f"role_{_suffix()}"
    perm_id = _permission_id_by_code(vendor, "billing_view")

    created = vendor.admin_create_role(code, "测试角色")["data"]["role"]
    assert created["builtin"] is False

    assigned = vendor.admin_update_role(created["id"], None, [perm_id])["data"]["role"]
    assert assigned["permissionIds"] == [perm_id]

    renamed = vendor.admin_update_role(created["id"], "测试角色改名", None)["data"]["role"]
    assert renamed["name"] == "测试角色改名"
    assert renamed["permissionIds"] == [perm_id]

    # 不存在的权限 id
    with pytest.raises(ApiError) as bad_perm:
        vendor.admin_update_role(created["id"], None, [99999999])
    assert bad_perm.value.code == -812

    vendor.admin_delete_role(created["id"])
    assert not any(r["code"] == code for r in vendor.admin_list_roles()["data"]["roles"])


def test_builtin_role_is_protected(seeded_org, logged_client):
    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)
    role = _role_by_code(vendor, "engineer")

    with pytest.raises(ApiError) as protected:
        vendor.admin_delete_role(role["id"])
    assert protected.value.code == -816


def test_role_in_use_cannot_be_deleted(seeded_org, logged_client):
    """团队管理员角色被 seeded_org 的成员占用：不是内置保护，而是引用保护"""
    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)
    code = f"role_used_{_suffix()}"
    role = vendor.admin_create_role(code, "占用角色")["data"]["role"]

    # 把 seeded_org 的管理员临时挂到这个角色上（直连数据库不可用则跳过）
    try:
        from conftest import _psql  # noqa: PLC0415  (夹具模块内的私有工具)
        _psql(f"update team_member set role_id = {role['id']} where user_id = {seeded_org.admin_id}")
    except Exception as e:
        pytest.skip(f"无法直连数据库占用角色: {e}")

    try:
        with pytest.raises(ApiError) as in_use:
            vendor.admin_delete_role(role["id"])
        assert in_use.value.code == -814
    finally:
        _psql(f"update team_member set role_id = "
              f"(select id from role where code = 'team_admin') where user_id = {seeded_org.admin_id}")
        vendor.admin_delete_role(role["id"])


def test_permission_group_crud(seeded_org, logged_client):
    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)
    name = f"组_{_suffix()}"
    perm_ids = [_permission_id_by_code(vendor, "billing_view")]

    created = vendor.admin_create_permission_group(name, perm_ids)["data"]["group"]
    listed = {g["id"]: g for g in vendor.admin_list_permission_groups()["data"]["groups"]}
    assert created["id"] in listed

    updated = vendor.admin_update_permission_group(created["id"], name + "_改", [])["data"]["group"]
    assert updated["name"] == name + "_改"
    assert updated["permissionIds"] == []

    renamed = vendor.admin_update_permission_group(created["id"], None, perm_ids)["data"]["group"]
    assert renamed["permissionIds"] == perm_ids

    vendor.admin_delete_permission_group(created["id"])
    assert not any(g["name"] == updated["name"]
                   for g in vendor.admin_list_permission_groups()["data"]["groups"])


def test_non_admin_cannot_touch_rbac(seeded_org, logged_client):
    admin = logged_client(seeded_org.admin_name, seeded_org.admin_password)

    with pytest.raises(ApiError) as denied:
        admin.admin_list_roles()
    assert denied.value.code == -807

    with pytest.raises(ApiError) as denied2:
        admin.admin_create_permission("x_perm", "m", "a")
    assert denied2.value.code == -807

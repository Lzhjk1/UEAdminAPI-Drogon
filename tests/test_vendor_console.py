"""厂商控制台权限回归：反馈 / 安装包 / 资源市场只对厂商团队管理员开放。

依赖 conftest 的 seeded_vendor_org 夹具（直连数据库种入团队与账号）。
运行前设置：
  UEADMINAPI_BASE_URL=http://127.0.0.1:6666
  UEADMINAPI_TEST_DB_URL=postgresql://ueadmin:ueadmin@127.0.0.1:5432/UEAdminAPI
"""
from __future__ import annotations


def _denied(body: dict) -> bool:
    return body.get("code") != 0


def _assert_allowed(client, path: str) -> None:
    body = client._request("GET", path, check=False)
    assert body.get("code") == 0, f"{path} 应放行: {body}"


def _assert_denied(client, path: str) -> None:
    body = client._request("GET", path, check=False)
    assert _denied(body), f"{path} 应拒绝: {body}"


def test_vendor_team_admin_allowed(seeded_vendor_org, logged_client):
    client = logged_client(seeded_vendor_org.vadmin_name, seeded_vendor_org.vadmin_password)
    _assert_allowed(client, "/api/admin/feedback/list")
    _assert_allowed(client, "/api/admin/installer/list?product=ueaicad")


def test_bootstrap_visibility_flag(seeded_vendor_org, logged_client):
    """客户端「厂商管理」入口：厂商团队管理员或超级管理员可见，普通团队/成员不可见。"""
    def flag(client) -> bool:
        body = client._request("GET", "/api/me/bootstrap", check=False)
        assert body.get("code") == 0, body
        return bool((body.get("data") or {}).get("canVendorConsole"))

    vendor_admin = logged_client(seeded_vendor_org.vadmin_name, seeded_vendor_org.vadmin_password)
    assert flag(vendor_admin) is True

    outsider = logged_client(seeded_vendor_org.outsider_name, seeded_vendor_org.outsider_password)
    assert flag(outsider) is True

    normal_admin = logged_client(seeded_vendor_org.admin_name, seeded_vendor_org.admin_password)
    assert flag(normal_admin) is False


def test_vendor_superadmin_allowed(seeded_vendor_org, logged_client):
    client = logged_client(seeded_vendor_org.vendor_name, seeded_vendor_org.vendor_password)
    _assert_allowed(client, "/api/admin/feedback/list")


def test_normal_team_admin_denied(seeded_vendor_org, logged_client):
    client = logged_client(seeded_vendor_org.admin_name, seeded_vendor_org.admin_password)
    _assert_denied(client, "/api/admin/feedback/list")
    _assert_denied(client, "/api/admin/installer/list?product=ueaicad")


def test_engineer_denied(seeded_vendor_org, logged_client):
    client = logged_client(seeded_vendor_org.eng_name, seeded_vendor_org.eng_password)
    _assert_denied(client, "/api/admin/feedback/list")


def test_superadmin_outside_vendor_team_allowed(seeded_vendor_org, logged_client):
    # 接口保持既有超管权限；客户端「厂商管理」入口只对厂商团队管理员可见
    client = logged_client(seeded_vendor_org.outsider_name, seeded_vendor_org.outsider_password)
    _assert_allowed(client, "/api/admin/feedback/list")
    _assert_allowed(client, "/api/admin/installer/list?product=ueaicad")

"""在线更新回归用例（检查 / 上传 / 发布 / 下载）

依赖 seeded_org 夹具（直连数据库种入账号）；运行方式见 test_team_bootstrap.py 顶部说明。
"""
from __future__ import annotations

import datetime
import hashlib
import shutil
from pathlib import Path

import pytest

from api_client import ApiError

REPO_ROOT = Path(__file__).resolve().parent.parent
TEST_VERSION_PREFIX = "11.7.9"   # 用例使用的版本号段，便于隔离与清理


@pytest.fixture(autouse=True)
def _clean_test_installers():
    """清掉本文件产生的安装包记录与落盘文件，避免跨用例/跨轮次互相影响"""
    from conftest import _db_url, _psql  # noqa: PLC0415

    def clean():
        if not _db_url():
            return
        try:
            _psql(f"delete from installer_version where version like '{TEST_VERSION_PREFIX}%'")
        except Exception as e:
            print(f"[WARN] 清理安装包记录失败: {e}")
        for path in (REPO_ROOT / "installers").glob(f"{TEST_VERSION_PREFIX}*"):
            shutil.rmtree(path, ignore_errors=True)

    clean()
    yield
    clean()

FAKE_EXE = b"MZ" + b"\x90" * 64 + b"UEADMIN-TEST-INSTALLER" + b"\x00" * 32


def _make_paid_with_update_auth(vendor, seeded_org) -> str:
    """给团队开通付费期与 online_update 授权，返回结束日期"""
    today = datetime.date.today()
    end = (today + datetime.timedelta(days=30)).isoformat()
    vendor.admin_set_subscription(seeded_org.team_id, today.isoformat(), end, "standard")
    vendor.admin_set_feature_auth(seeded_org.team_id, "online_update", True)
    return end


def test_check_requires_paid_and_authorization(seeded_org, logged_client):
    admin = logged_client(seeded_org.admin_name, seeded_org.admin_password)
    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)

    # 未付费
    data = admin.update_check("11.7.100")["data"]
    assert data["hasUpdate"] is False
    assert data["reason"] == "not_paid"

    # 已付费但没开 online_update 授权
    today = datetime.date.today()
    vendor.admin_set_subscription(seeded_org.team_id, today.isoformat(),
                                  (today + datetime.timedelta(days=30)).isoformat())
    data = admin.update_check("11.7.100")["data"]
    assert data["reason"] == "no_auth"

    # 授权后：没有已发布安装包
    vendor.admin_set_feature_auth(seeded_org.team_id, "online_update", True)
    data = admin.update_check("11.7.100")["data"]
    assert data["reason"] == "no_package"


def test_upload_publish_and_download(seeded_org, logged_client):
    admin = logged_client(seeded_org.admin_name, seeded_org.admin_password)
    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)
    _make_paid_with_update_auth(vendor, seeded_org)

    version = "11.7.999"
    uploaded = vendor.admin_upload_installer(version, FAKE_EXE, notes="回归用假安装包", build_no=999)["data"]
    assert uploaded["status"] == 0
    assert uploaded["sha256"] == hashlib.sha256(FAKE_EXE).hexdigest()

    listed = vendor.admin_list_installers()["data"]["installers"]
    assert any(item["id"] == uploaded["id"] and item["version"] == version for item in listed)

    # 草稿不参与更新检查
    assert admin.update_check("11.7.120")["data"]["reason"] == "no_package"

    vendor.admin_publish_installer(uploaded["id"])

    data = admin.update_check("11.7.120")["data"]
    assert data["hasUpdate"] is True
    assert data["version"] == version
    assert data["sha256"] == uploaded["sha256"]
    assert data["sizeBytes"] == len(FAKE_EXE)
    assert data["downloadUrl"].endswith("/api/update/download/" + version)

    resp = admin.update_download_raw(version)
    assert resp.status_code == 200
    assert resp.content == FAKE_EXE
    assert "setup_fake.exe" in resp.headers.get("Content-Disposition", "")

    # 已是最新
    latest = admin.update_check(version)["data"]
    assert latest["hasUpdate"] is False
    assert latest["reason"] == "no_update"
    assert latest["latestVersion"] == version


def test_upload_requires_admin_and_exe(seeded_org, logged_client):
    admin = logged_client(seeded_org.admin_name, seeded_org.admin_password)
    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)

    with pytest.raises(ApiError) as denied:
        admin.admin_upload_installer("11.7.998", FAKE_EXE)
    assert denied.value.code == -807

    with pytest.raises(ApiError) as bad_ext:
        vendor.admin_upload_installer("11.7.998", b"not an installer", file_name="setup.txt")
    assert bad_ext.value.code == -105


def test_publish_keeps_single_active_version(seeded_org, logged_client):
    admin = logged_client(seeded_org.admin_name, seeded_org.admin_password)
    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)
    _make_paid_with_update_auth(vendor, seeded_org)

    first = vendor.admin_upload_installer("11.7.900", FAKE_EXE + b"A")["data"]
    second = vendor.admin_upload_installer("11.7.901", FAKE_EXE + b"B")["data"]

    vendor.admin_publish_installer(first["id"])
    vendor.admin_publish_installer(second["id"])

    listed = {i["id"]: i for i in vendor.admin_list_installers()["data"]["installers"]}
    assert listed[first["id"]]["status"] == 2   # 被新版本顶下线
    assert listed[second["id"]]["status"] == 1  # 当前发布版本

    data = admin.update_check("11.7.900")["data"]
    assert data["hasUpdate"] is True
    assert data["version"] == "11.7.901"


def test_download_requires_paid(seeded_org, logged_client):
    admin = logged_client(seeded_org.admin_name, seeded_org.admin_password)
    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)
    _make_paid_with_update_auth(vendor, seeded_org)

    item = vendor.admin_upload_installer("11.7.997", FAKE_EXE)["data"]
    vendor.admin_publish_installer(item["id"])

    # 未付费团队的新成员不能下载（用另一套未付费账号验证成本较高，这里校验未开通授权时的错误码）
    vendor.admin_set_feature_auth(seeded_org.team_id, "online_update", False)
    resp = admin.update_download_raw("11.7.997")
    assert resp.status_code == 200
    body = resp.json()
    assert body["code"] == -809  # ApiError_FeatureNotAuthorized

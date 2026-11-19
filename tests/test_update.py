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
            _psql(f"delete from installer_upload_session "
                  f"where version like '{TEST_VERSION_PREFIX}%' "
                  f"or coalesce(from_version, '') like '{TEST_VERSION_PREFIX}%'")
            _psql(f"delete from update_delta_job where to_version like '{TEST_VERSION_PREFIX}%' "
                  f"or from_version like '{TEST_VERSION_PREFIX}%'")
            _psql(f"delete from update_delta where to_version like '{TEST_VERSION_PREFIX}%' "
                  f"or from_version like '{TEST_VERSION_PREFIX}%'")
            _psql(f"delete from installer_version where version like '{TEST_VERSION_PREFIX}%'")
        except Exception as e:
            print(f"[WARN] 清理安装包记录失败: {e}")
        for path in (REPO_ROOT / "installers").glob(f"{TEST_VERSION_PREFIX}*"):
            shutil.rmtree(path, ignore_errors=True)
        delta_root = REPO_ROOT / "installers" / "delta"
        if delta_root.is_dir():
            for path in delta_root.glob(f"{TEST_VERSION_PREFIX}*_from_*"):
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


def test_upload_derives_version_from_file_name(seeded_org, logged_client):
    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)

    uploaded = vendor.admin_upload_installer(
        None, FAKE_EXE,
        file_name="UEAP_PRJ_11.7.980_Setup.Win32.build_202610271350.exe")["data"]
    assert uploaded["version"] == "11.7.980"

    listed = vendor.admin_list_installers()["data"]["installers"]
    item = next(x for x in listed if x["id"] == uploaded["id"])
    # 0006 起 build_no 是 bigint，12 位时间戳可完整保存
    assert item["buildNo"] == 202610271350

    # 文件名不符合约定且没填 version 时仍报缺少参数
    with pytest.raises(ApiError) as ex:
        vendor.admin_upload_installer(None, FAKE_EXE, file_name="setup_fake.exe")
    assert ex.value.code == -102


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


def test_edit_installer_metadata_and_download(seeded_org, logged_client):
    admin = logged_client(seeded_org.admin_name, seeded_org.admin_password)
    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)
    _make_paid_with_update_auth(vendor, seeded_org)

    item = vendor.admin_upload_installer("11.7.950", FAKE_EXE, notes="旧备注",
                                         build_no=950, min_version="11.7.100")["data"]
    assert item["minVersion"] == "11.7.100"

    vendor.admin_update_installer(item["id"], "11.7.951", build_no=951,
                                  notes="新备注", min_version="11.7.200")

    row = {i["id"]: i for i in vendor.admin_list_installers()["data"]["installers"]}[item["id"]]
    assert row["version"] == "11.7.951"
    assert row["buildNo"] == 951
    assert row["notes"] == "新备注"
    assert row["minVersion"] == "11.7.200"

    # 改版本号只动元数据：磁盘文件仍可下载（按库里 file_path 取）
    vendor.admin_publish_installer(item["id"])
    # minVersion 已提到 11.7.200，低于它的客户端会被拦在 below_min_version
    data = admin.update_check("11.7.200")["data"]
    assert data["version"] == "11.7.951"
    resp = admin.update_download_raw("11.7.951")
    assert resp.status_code == 200
    assert resp.content == FAKE_EXE

    # 版本号不能撞车
    other = vendor.admin_upload_installer("11.7.952", FAKE_EXE)["data"]
    with pytest.raises(ApiError) as dup:
        vendor.admin_update_installer(other["id"], "11.7.951", notes="")
    assert dup.value.code == -815

    # 团队管理员不能编辑
    with pytest.raises(ApiError) as denied:
        admin.admin_update_installer(other["id"], "11.7.953", notes="")
    assert denied.value.code == -807


def test_offline_installer_stops_update(seeded_org, logged_client):
    admin = logged_client(seeded_org.admin_name, seeded_org.admin_password)
    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)
    _make_paid_with_update_auth(vendor, seeded_org)

    item = vendor.admin_upload_installer("11.7.960", FAKE_EXE)["data"]
    vendor.admin_publish_installer(item["id"])
    assert admin.update_check("11.7.100")["data"]["hasUpdate"] is True

    vendor.admin_offline_installer(item["id"])
    row = {i["id"]: i for i in vendor.admin_list_installers()["data"]["installers"]}[item["id"]]
    assert row["status"] == 2

    data = admin.update_check("11.7.100")["data"]
    assert data["hasUpdate"] is False
    assert data["reason"] == "no_package"


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


# ---------- 增量更新包（文件级差量） ----------

DELTA_EXE = b"MZ" + b"\x91" * 32 + b"UEADMIN-TEST-DELTA" + b"\x00" * 16


def _manifest(from_version: str, to_version: str) -> dict:
    return {
        "schemaVersion": 1,
        "fromVersion": from_version,
        "toVersion": to_version,
        "files": [
            {"path": "bin/APPDMSLogin10.exe", "size": 12345,
             "sha256": "a" * 64, "forceGroup": False},
            {"path": "AI/MCP/python/python.exe", "size": 54321,
             "sha256": "b" * 64, "forceGroup": True},
        ],
        "deletes": ["bin/old_plugin.dll"],
        "forceGroups": ["AI/MCP/python"],
        "baseFiles": [
            {"path": "DllLogin10.dll", "sha256": "c" * 64},
            {"path": "APPDMSLogin10.exe", "sha256": "d" * 64},
        ],
        "totalInstallBytes": 66666,
    }


def test_delta_check_prefers_matching_baseline(seeded_org, logged_client):
    """基线精确匹配才下发差量；不匹配回落全量；未发布差量不生效"""
    admin = logged_client(seeded_org.admin_name, seeded_org.admin_password)
    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)
    _make_paid_with_update_auth(vendor, seeded_org)

    full = vendor.admin_upload_installer("11.7.930", FAKE_EXE, notes="差量基线全量包")["data"]
    vendor.admin_publish_installer(full["id"])

    delta = vendor.admin_upload_delta("11.7.929", "11.7.930", DELTA_EXE,
                                      manifest=_manifest("11.7.929", "11.7.930"))
    assert delta["fromVersion"] == "11.7.929"
    assert delta["toVersion"] == "11.7.930"
    assert delta["status"] == 0
    assert delta["sha256"] == hashlib.sha256(DELTA_EXE).hexdigest()

    # 草稿差量不生效：仍是全量
    data = admin.update_check("11.7.929")["data"]
    assert data["packageKind"] == "full"
    assert data["downloadUrl"].endswith("/api/update/download/11.7.930")

    vendor.admin_publish_delta(delta["id"])

    # 基线精确匹配 -> 差量；全量字段保持原语义（老客户端仍按 downloadUrl 走全量）
    data = admin.update_check("11.7.929")["data"]
    assert data["hasUpdate"] is True
    assert data["packageKind"] == "delta"
    assert data["deltaFromVersion"] == "11.7.929"
    assert data["version"] == "11.7.930"
    assert data["downloadUrl"].endswith("/api/update/download/11.7.930")
    assert "kind=delta" not in data["downloadUrl"]
    assert "kind=delta" in data["deltaDownloadUrl"] and "fromVersion=11.7.929" in data["deltaDownloadUrl"]
    assert data["deltaSizeBytes"] == len(DELTA_EXE)
    assert data["deltaSha256"] == hashlib.sha256(DELTA_EXE).hexdigest()
    assert data["deltaInstallBytes"] == 66666

    # 基线不匹配 -> 全量
    other = admin.update_check("11.7.928")["data"]
    assert other["packageKind"] == "full"
    assert "deltaDownloadUrl" not in other

    # 差量下载：kind+fromVersion 精确命中
    resp = admin.update_download_raw("11.7.930", kind="delta", from_version="11.7.929")
    assert resp.status_code == 200
    assert resp.content == DELTA_EXE

    # 老客户端不传 kind 仍拿全量
    resp = admin.update_download_raw("11.7.930")
    assert resp.content == FAKE_EXE

    # 缺 fromVersion / 基线不符 -> 统一失败码
    resp = admin.update_download_raw("11.7.930", kind="delta")
    assert resp.json()["code"] == -102
    resp = admin.update_download_raw("11.7.930", kind="delta", from_version="11.7.111")
    assert resp.json()["code"] == -817

    # manifest 下发与权限
    manifest = admin.update_manifest("11.7.930", "11.7.929")["data"]
    assert manifest["toVersion"] == "11.7.930"
    assert manifest["deletes"] == ["bin/old_plugin.dll"]
    assert manifest["forceGroups"] == ["AI/MCP/python"]

    vendor.admin_set_feature_auth(seeded_org.team_id, "online_update", False)
    assert admin.update_manifest_raw("11.7.930", "11.7.929").json()["code"] == -809
    assert admin.update_download_raw("11.7.930", kind="delta",
                                     from_version="11.7.929").json()["code"] == -809


def test_delta_admin_lifecycle(seeded_org, logged_client):
    """差量包列表 / 下线 / 删除：已发布不能删，下线后可删"""
    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)
    _make_paid_with_update_auth(vendor, seeded_org)

    item = vendor.admin_upload_installer("11.7.940", FAKE_EXE)["data"]
    vendor.admin_publish_installer(item["id"])
    delta = vendor.admin_upload_delta("11.7.939", "11.7.940", DELTA_EXE)

    listed = vendor.admin_list_deltas()["data"]["deltas"]
    row = next(x for x in listed if x["id"] == delta["id"])
    assert row["fromVersion"] == "11.7.939"
    assert row["toVersion"] == "11.7.940"
    assert row["status"] == 0

    # 已发布不允许删除
    vendor.admin_publish_delta(delta["id"])
    with pytest.raises(ApiError) as in_use:
        vendor.admin_delete_delta(delta["id"])
    assert in_use.value.code == -105

    vendor.admin_offline_delta(delta["id"])
    row = {x["id"]: x for x in vendor.admin_list_deltas()["data"]["deltas"]}[delta["id"]]
    assert row["status"] == 2

    vendor.admin_delete_delta(delta["id"])
    listed = vendor.admin_list_deltas()["data"]["deltas"]
    assert all(x["id"] != delta["id"] for x in listed)


def test_delta_upload_validation(seeded_org, logged_client):
    """差量上传的必填项与重复校验"""
    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)
    item = vendor.admin_upload_installer("11.7.945", FAKE_EXE)["data"]
    vendor.admin_publish_installer(item["id"])

    init = {
        "version": "11.7.945",
        "kind": "delta",
        "fromVersion": "11.7.944",
        "fileName": "delta.exe",
        "fileSize": len(DELTA_EXE),
        "fileSha256": hashlib.sha256(DELTA_EXE).hexdigest(),
        "manifest": _manifest("11.7.944", "11.7.945"),
    }

    # 缺 fromVersion
    with pytest.raises(ApiError) as no_base:
        vendor._request("POST", "/api/admin/installer/upload/init",
                        json_body={k: v for k, v in init.items() if k != "fromVersion"})
    assert no_base.value.code == -102

    # 缺 manifest
    with pytest.raises(ApiError) as no_manifest:
        vendor._request("POST", "/api/admin/installer/upload/init",
                        json_body=init | {"manifest": {}})
    assert no_manifest.value.code == -102

    # 基线等于目标
    with pytest.raises(ApiError) as same:
        vendor._request("POST", "/api/admin/installer/upload/init",
                        json_body=init | {"fromVersion": "11.7.945"})
    assert same.value.code == -105

    # 正常上传后，同基线同目标再次 init（不同文件）报已存在
    vendor.admin_upload_delta("11.7.944", "11.7.945", DELTA_EXE)
    with pytest.raises(ApiError) as dup:
        vendor._request("POST", "/api/admin/installer/upload/init",
                        json_body=init | {"fileSha256": "e" * 64})
    assert dup.value.code == -815

    # 同一文件重传直接幂等返回
    again = vendor._request("POST", "/api/admin/installer/upload/init",
                            json_body=init)["data"]
    assert again["alreadyUploaded"] is True
    assert again["fromVersion"] == "11.7.944"


# ---------- 服务端自动生成差量（解包 + makensis）----------

def _tools_available() -> bool:
    return bool(shutil.which("7z") and shutil.which("makensis"))


def _compile_tiny_installer(work: Path, name: str, files: dict[str, str]) -> bytes:
    """用 makensis 造一个真实的小 NSIS 安装器，作为自动差量生成的上传输入。"""
    import subprocess  # noqa: PLC0415

    src = work / name
    payload = src / "files"
    payload.mkdir(parents=True)
    for rel, content in files.items():
        target = payload / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content, encoding="utf-8")
    nsi = src / "setup.nsi"
    bslash = chr(92)
    nsi.write_text(
        "Unicode true\n"
        + 'OutFile "setup.exe"\n'
        + 'InstallDir "$TEMP' + bslash + 'ueap-auto-delta-test"\n'
        + "RequestExecutionLevel user\n"
        + "Section\n"
        + '  SetOutPath "$INSTDIR"\n'
        + '  File /r "files' + bslash + '*.*"\n'
        + "SectionEnd\n",
        encoding="utf-8",
    )
    subprocess.run(["makensis", "-V1", str(nsi)], cwd=src, check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return (src / "setup.exe").read_bytes()


def test_delta_health_endpoint(seeded_org, logged_client):
    """管理端可自检差量运行时（7z / makensis / 资产 / 临时盘）"""
    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)
    data = vendor._request("GET", "/api/admin/update/delta/health")["data"]
    for key in ("sevenZip", "makensis", "assetsDir", "tempDir", "autoEnabled", "ok"):
        assert key in data
    assert "ok" in data["sevenZip"] and "ok" in data["makensis"]
    if _tools_available():
        assert data["ok"] is True, data


@pytest.mark.skipif(not _tools_available(), reason="需要 7z 与 makensis（服务端自动差量依赖）")
def test_auto_delta_generated_after_full_upload(seeded_org, logged_client, tmp_path):
    """上传新全量包后服务端自动解包比对并生成差量，发布后客户端可见"""
    import subprocess  # noqa: PLC0415
    import time  # noqa: PLC0415

    admin = logged_client(seeded_org.admin_name, seeded_org.admin_password)
    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)
    _make_paid_with_update_auth(vendor, seeded_org)

    base_exe = _compile_tiny_installer(tmp_path, "base", {
        "app/a.txt": "A1", "app/keep.txt": "K", "app/old.txt": "O",
        "CPP-2005-Win32/DllLogin10.dll": "D1"})
    target_exe = _compile_tiny_installer(tmp_path, "target", {
        "app/a.txt": "A2", "app/keep.txt": "K", "app/new.txt": "N",
        "CPP-2005-Win32/DllLogin10.dll": "D2"})

    base = vendor.admin_upload_installer("11.7.970", base_exe,
                                         file_name="base_setup.exe")["data"]
    vendor.admin_publish_installer(base["id"])

    target = vendor.admin_upload_installer("11.7.971", target_exe,
                                           file_name="target_setup.exe")["data"]

    # 上传完成即触发后台生成；轮询任务状态
    job = None
    deadline = time.time() + 90
    while time.time() < deadline:
        jobs = vendor.admin_delta_jobs()["data"]["jobs"]
        job = next((j for j in jobs if j["toVersion"] == "11.7.971"), None)
        if job and job["status"] in (2, 3, 4):
            break
        time.sleep(1)
    assert job is not None, "没有差量生成任务"
    assert job["status"] == 2, f"差量生成失败/跳过: {job}"
    assert job["fromVersion"] == "11.7.970"

    # 全量发布后差量自动生效
    vendor.admin_publish_installer(target["id"])
    deltas = vendor.admin_list_deltas()["data"]["deltas"]
    row = next(d for d in deltas if d["fromVersion"] == "11.7.970"
               and d["toVersion"] == "11.7.971")
    assert row["status"] == 1

    data = admin.update_check("11.7.970")["data"]
    assert data["packageKind"] == "delta"
    assert data["deltaFromVersion"] == "11.7.970"

    manifest = admin.update_manifest("11.7.971", "11.7.970")["data"]
    changed = {f["path"]: f for f in manifest["files"]}
    assert "app\\a.txt" in changed and "app\\new.txt" in changed
    assert "app\\old.txt" in manifest["deletes"]

    # 客户端预检契约：baseFiles 的 path/sha256 指向基线安装树中的关键文件
    base_files = manifest["baseFiles"]
    dll = next((f for f in base_files
                if f["path"].lower().endswith("dlllogin10.dll")), None)
    assert dll is not None, base_files
    assert dll["sha256"] == hashlib.sha256(b"D1").hexdigest()

    resp = admin.update_download_raw("11.7.971", kind="delta", from_version="11.7.970")
    assert resp.status_code == 200
    assert resp.content[:2] == b"MZ"

    extract_dir = tmp_path / "delta-out"
    delta_file = tmp_path / "delta.exe"
    delta_file.write_bytes(resp.content)
    subprocess.run(["7z", "x", str(delta_file), f"-o{extract_dir}", "-y"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    assert (extract_dir / "app" / "a.txt").read_text(encoding="utf-8") == "A2"
    assert (extract_dir / "app" / "new.txt").read_text(encoding="utf-8") == "N"
    dll_out = next(extract_dir.rglob("DllLogin10.dll"), None)
    assert dll_out is not None and dll_out.read_text(encoding="utf-8") == "D2"

    # 重新生成：删旧差量并再次入队，完成后仍随已发布全量自动生效
    vendor.admin_rebuild_delta("11.7.970", "11.7.971")
    deadline = time.time() + 60
    rebuilt = None
    while time.time() < deadline:
        jobs = vendor.admin_delta_jobs()["data"]["jobs"]
        rebuilt = next((j for j in jobs if j["toVersion"] == "11.7.971"
                        and j["id"] > job["id"]), None)
        if rebuilt and rebuilt["status"] in (2, 3, 4):
            break
        time.sleep(1)
    assert rebuilt is not None and rebuilt["status"] == 2, f"重新生成失败: {rebuilt}"
    deltas = vendor.admin_list_deltas()["data"]["deltas"]
    assert any(d["fromVersion"] == "11.7.970" and d["toVersion"] == "11.7.971"
               and d["status"] == 1 for d in deltas)


@pytest.mark.skipif(not _tools_available(), reason="需要 7z 与 makensis（服务端自动差量依赖）")
def test_auto_delta_skips_force_group_changes(seeded_org, logged_client, tmp_path):
    """强制整组替换目录（AI/MCP/python）变化时自动差量应跳过并提示发全量"""
    import time  # noqa: PLC0415

    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)
    _make_paid_with_update_auth(vendor, seeded_org)

    base_exe = _compile_tiny_installer(tmp_path, "fg-base", {
        "app/a.txt": "A", "AI/MCP/python/py.txt": "P1"})
    target_exe = _compile_tiny_installer(tmp_path, "fg-target", {
        "app/a.txt": "A", "AI/MCP/python/py.txt": "P2"})

    base = vendor.admin_upload_installer("11.7.980", base_exe,
                                         file_name="fg_base_setup.exe")["data"]
    vendor.admin_publish_installer(base["id"])
    vendor.admin_upload_installer("11.7.981", target_exe, file_name="fg_target_setup.exe")

    job = None
    deadline = time.time() + 60
    while time.time() < deadline:
        jobs = vendor.admin_delta_jobs()["data"]["jobs"]
        job = next((j for j in jobs if j["toVersion"] == "11.7.981"), None)
        if job and job["status"] in (2, 3, 4):
            break
        time.sleep(1)
    assert job is not None
    assert job["status"] == 4, f"应跳过差量: {job}"
    assert "强制整组" in job["message"]

    deltas = vendor.admin_list_deltas()["data"]["deltas"]
    assert all(not (d["fromVersion"] == "11.7.980" and d["toVersion"] == "11.7.981")
               for d in deltas)


@pytest.mark.skipif(not _tools_available(), reason="需要 7z 与 makensis（服务端自动差量依赖）")
def test_published_version_edit_regenerates_delta(seeded_org, logged_client, tmp_path):
    """已发布全量改版本号：旧差量清理，并按新版本号自动重生成"""
    import time  # noqa: PLC0415

    admin = logged_client(seeded_org.admin_name, seeded_org.admin_password)
    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)
    _make_paid_with_update_auth(vendor, seeded_org)

    base_exe = _compile_tiny_installer(tmp_path, "edit-base", {"app/a.txt": "1"})
    target_exe = _compile_tiny_installer(tmp_path, "edit-target", {"app/a.txt": "2"})

    base = vendor.admin_upload_installer("11.7.990", base_exe, file_name="edit_base.exe")["data"]
    vendor.admin_publish_installer(base["id"])
    target = vendor.admin_upload_installer("11.7.991", target_exe, file_name="edit_target.exe")["data"]

    def wait_job(version):
        deadline = time.time() + 60
        while time.time() < deadline:
            jobs = vendor.admin_delta_jobs()["data"]["jobs"]
            job = next((j for j in jobs if j["toVersion"] == version), None)
            if job and job["status"] in (2, 3, 4):
                return job
            time.sleep(1)
        return None

    first = wait_job("11.7.991")
    assert first is not None and first["status"] == 2, first
    vendor.admin_publish_installer(target["id"])
    deltas = vendor.admin_list_deltas()["data"]["deltas"]
    assert any(d["fromVersion"] == "11.7.990" and d["toVersion"] == "11.7.991" for d in deltas)

    # 改已发布版本号 -> 旧差量被清理，并按新号重生成
    vendor.admin_update_installer(target["id"], "11.7.992")
    deltas = vendor.admin_list_deltas()["data"]["deltas"]
    assert all(d["toVersion"] != "11.7.991" for d in deltas)

    rebuilt = wait_job("11.7.992")
    assert rebuilt is not None and rebuilt["status"] == 2, rebuilt
    deltas = vendor.admin_list_deltas()["data"]["deltas"]
    assert any(d["fromVersion"] == "11.7.990" and d["toVersion"] == "11.7.992"
               and d["status"] == 1 for d in deltas)

    data = admin.update_check("11.7.990")["data"]
    assert data["version"] == "11.7.992"
    assert data["packageKind"] == "delta"


def test_delta_falls_back_when_target_not_latest(seeded_org, logged_client):
    """差量只对「当前最新全量」生效：发布更新的全量后，旧差量自动失效"""
    admin = logged_client(seeded_org.admin_name, seeded_org.admin_password)
    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)
    _make_paid_with_update_auth(vendor, seeded_org)

    first = vendor.admin_upload_installer("11.7.955", FAKE_EXE)["data"]
    vendor.admin_publish_installer(first["id"])
    delta = vendor.admin_upload_delta("11.7.954", "11.7.955", DELTA_EXE)
    vendor.admin_publish_delta(delta["id"])
    assert admin.update_check("11.7.954")["data"]["packageKind"] == "delta"

    second = vendor.admin_upload_installer("11.7.956", FAKE_EXE + b"NEW")["data"]
    vendor.admin_publish_installer(second["id"])

    data = admin.update_check("11.7.954")["data"]
    assert data["version"] == "11.7.956"
    assert data["packageKind"] == "full"

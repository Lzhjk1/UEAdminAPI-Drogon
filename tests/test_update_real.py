"""真实发布包级别的自动差量验证（默认跳过，按需开启）。

用法（把两个真实全量安装器喂给本地服务）：
  export UEADMINAPI_REAL_BASE_EXE="/mnt/d/AutoPDMS安装包/UEAP_PRJ_11.7.124_Setup...exe"
  export UEADMINAPI_REAL_TARGET_EXE="/mnt/d/AutoPDMS安装包/UEAP_PRJ_11.7.125_Setup...exe"
  export UEADMINAPI_BASE_URL=http://127.0.0.1:6666
  export UEADMINAPI_TEST_DB_URL=postgresql://ueadmin:ueadmin@127.0.0.1:5432/UEAdminAPI
  python3 -m pytest test_update_real.py -q -s

依赖服务端安装 7z / makensis，且 DeltaTempDir 能放下两份约 4GB 安装树。
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import time
from pathlib import Path

import pytest

from api_client import ApiError

REPO_ROOT = Path(__file__).resolve().parent.parent

BASE_EXE = os.environ.get("UEADMINAPI_REAL_BASE_EXE", "")
TARGET_EXE = os.environ.get("UEADMINAPI_REAL_TARGET_EXE", "")


def _version_from_name(name: str) -> str:
    m = re.search(r"UEAP_PRJ_(\d+(?:\.\d+)+)_", name, re.I)
    assert m, f"无法从文件名解析版本号: {name}"
    return m.group(1)


def _clean_versions(versions: list[str]):
    from conftest import _db_url, _psql  # noqa: PLC0415

    if _db_url():
        for version in versions:
            for sql in (
                f"delete from update_delta_job where to_version = '{version}' "
                f"or from_version = '{version}'",
                f"delete from update_delta where to_version = '{version}' "
                f"or from_version = '{version}'",
                f"delete from installer_upload_session where version = '{version}'",
                f"delete from installer_version where version = '{version}'",
            ):
                try:
                    _psql(sql)
                except Exception as e:  # 清理失败不阻断
                    print(f"[WARN] 清理失败: {sql} -> {e}")
    for version in versions:
        shutil.rmtree(REPO_ROOT / "installers" / version, ignore_errors=True)
    delta_root = REPO_ROOT / "installers" / "delta"
    if delta_root.is_dir():
        for path in delta_root.glob("*"):
            if any(version in path.name for version in versions):
                shutil.rmtree(path, ignore_errors=True)


@pytest.mark.skipif(not (BASE_EXE and TARGET_EXE),
                    reason="需要 UEADMINAPI_REAL_BASE_EXE / UEADMINAPI_REAL_TARGET_EXE")
@pytest.mark.skipif(not shutil.which("7z"), reason="需要 7z 校验差量内容")
def test_auto_delta_from_real_release_installers(seeded_org, logged_client, tmp_path):
    from test_update import _make_paid_with_update_auth  # noqa: PLC0415

    base_path = Path(BASE_EXE)
    target_path = Path(TARGET_EXE)
    assert base_path.is_file() and target_path.is_file()

    base_version = _version_from_name(base_path.name)
    target_version = _version_from_name(target_path.name)
    assert base_version != target_version

    admin = logged_client(seeded_org.admin_name, seeded_org.admin_password)
    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)
    _make_paid_with_update_auth(vendor, seeded_org)
    _clean_versions([base_version, target_version])

    try:
        base = vendor.admin_upload_installer_chunked(base_version, base_path)
        vendor.admin_publish_installer(base["id"])

        target = vendor.admin_upload_installer_chunked(target_version, target_path)

        # 自动生成任务（解包两份 543MB + 比对约 6600 文件，给足时间）
        job = None
        deadline = time.time() + 480
        while time.time() < deadline:
            jobs = vendor.admin_delta_jobs()["data"]["jobs"]
            job = next((j for j in jobs if j["toVersion"] == target_version), None)
            if job and job["status"] in (2, 3, 4):
                break
            time.sleep(3)
        assert job is not None, "没有自动差量任务"
        assert job["status"] == 2, f"自动生成失败/跳过: {job}"
        assert job["fromVersion"] == base_version

        vendor.admin_publish_installer(target["id"])
        deltas = vendor.admin_list_deltas()["data"]["deltas"]
        row = next(d for d in deltas
                   if d["fromVersion"] == base_version and d["toVersion"] == target_version)
        assert row["status"] == 1

        # 差量体积目标：≤ 全量包 20%
        full_size = target_path.stat().st_size
        ratio = row["sizeBytes"] / full_size
        print(f"[real] delta={row['sizeBytes']} full={full_size} ratio={ratio:.4%}")
        assert ratio <= 0.20, f"差量包占比过高: {ratio:.2%}"

        # 内容抽查：11.7.124 -> 125 的变更是 CPP-2005-Win32/DllLogin10.dll
        data = admin.update_check(base_version)["data"]
        assert data["packageKind"] == "delta"

        resp = admin.update_download_raw(target_version, kind="delta", from_version=base_version)
        assert resp.status_code == 200 and resp.content[:2] == b"MZ"
        delta_file = tmp_path / "delta.exe"
        delta_file.write_bytes(resp.content)
        extract_dir = tmp_path / "delta-out"
        subprocess.run(["7z", "x", str(delta_file), f"-o{extract_dir}", "-y"],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        delta_dll = next(extract_dir.rglob("DllLogin10.dll"), None)
        assert delta_dll is not None, "差量包缺少 DllLogin10.dll"

        target_dll_dir = tmp_path / "target-dll"
        subprocess.run(["7z", "e", str(target_path), "CPP-2005-Win32/DllLogin10.dll",
                        f"-o{target_dll_dir}", "-y"],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        target_dll = next(target_dll_dir.rglob("DllLogin10.dll"))
        assert delta_dll.read_bytes() == target_dll.read_bytes()
    finally:
        _clean_versions([base_version, target_version])

"""客户端环境信息上报回归：提交 / 去重 / 超管列表 / 权限隔离

依赖 seeded_org 夹具（直连数据库种入厂商超管与团队管理员）；运行方式：

  UEADMINAPI_BASE_URL=http://127.0.0.1:6767 \
  UEADMINAPI_TEST_DB_URL=postgresql://ueadmin:ueadmin@127.0.0.1:5432/ueadmin_envtest \
  python -m pytest tests/test_client_env.py -q
"""
from __future__ import annotations

import uuid

import pytest

from api_client import ApiError
from conftest import _psql, _sql_lit


def _payload(device_key: str, gpu: str = "NVIDIA RTX A2000") -> dict:
    return {
        "schemaVersion": 1,
        "deviceKey": device_key,
        "productVersion": "11.7.125",
        "cad": {"product": "AutoCAD 2024", "version": "24.3s", "bits": "64", "lang": "简体中文"},
        "os": {
            "name": "Windows 11 企业版",
            "version": "24H2",
            "build": "26100.2314",
            "arch": "x64",
            "lang": "中文（简体，中国）",
            "timezone": "UTC+08:00",
        },
        "cpu": {"name": "Intel Core i7-12700", "cores": 12, "logical": 20},
        "memory": {"totalMb": 32768},
        "gpu": {"name": gpu, "driver": "553.62"},
    }


@pytest.fixture
def cleanup_env(seeded_org):
    yield
    try:
        _psql(f"delete from client_env_report where user_id = {seeded_org.vendor_id}")
    except Exception as exc:  # 清理失败不阻断用例
        print(f"[WARN] 清理环境上报失败: {exc}")


def test_submit_dedupe_and_admin_list(seeded_org, logged_client, cleanup_env):
    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)
    device_key = uuid.uuid4().hex + uuid.uuid4().hex[:16]

    body = vendor._request("POST", "/api/client/env", json_body=_payload(device_key))
    assert body["code"] == 0 and body["data"]["ok"] is True

    # 同一用户同一设备重复上报：覆盖更新而不是新增一行
    vendor._request("POST", "/api/client/env", json_body=_payload(device_key, gpu="NVIDIA RTX 4090"))
    count = int(_psql(
        "select count(*) from client_env_report where device_key = " + _sql_lit(device_key)
    )[0])
    assert count == 1

    listing = vendor._request("GET", "/api/admin/client/env/list", params={"page": 1, "size": 200})
    assert listing["code"] == 0
    items = [item for item in listing["data"]["items"] if item["deviceKey"] == device_key]
    assert len(items) == 1
    item = items[0]
    assert item["userId"] == seeded_org.vendor_id
    assert item["productVersion"] == "11.7.125"
    assert item["cadProduct"] == "AutoCAD 2024"
    assert "RTX 4090" in item["payload"]
    assert item["lastSeen"] >= item["firstSeen"]


def test_submit_requires_device_key(seeded_org, logged_client):
    vendor = logged_client(seeded_org.vendor_name, seeded_org.vendor_password)
    with pytest.raises(ApiError) as err:
        vendor._request("POST", "/api/client/env", json_body={"productVersion": "11.7.125"})
    assert err.value.code == -102


def test_list_requires_super_admin(seeded_org, logged_client):
    # privilege=3：团队管理员，没有厂商 Admin 位(4)，也没有 super_admin 系统角色
    admin = logged_client(seeded_org.admin_name, seeded_org.admin_password)
    resp = admin._raw_request("GET", "/api/admin/client/env/list")
    assert resp.status_code == 403

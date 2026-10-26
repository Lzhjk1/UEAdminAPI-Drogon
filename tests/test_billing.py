"""余额 / 模拟支付回归用例

依赖 conftest 的 seeded_org 夹具（直连数据库种入账号）。
运行前设置 UEADMINAPI_BASE_URL 与 UEADMINAPI_TEST_DB_URL（见 test_team_bootstrap.py）。
"""
from __future__ import annotations

import pytest

from api_client import ApiClient, ApiError

PNG_MAGIC = b"\x89PNG\r\n\x1a\n"


def _create_order(client: ApiClient, amount_cents: int, channel: int = 9,
                  idem_key: str | None = None) -> dict:
    return client.create_recharge_order(amount_cents, channel, idem_key)["data"]


def test_create_order_is_idempotent_by_idem_key(seeded_org, logged_client):
    client = logged_client(seeded_org.admin_name, seeded_org.admin_password)

    first = _create_order(client, 10000, 9, "itest-idem")
    second = _create_order(client, 10000, 9, "itest-idem")

    assert first["orderNo"] == second["orderNo"]
    assert first["status"] == 0
    assert first["qrPngUrl"].endswith("/api/bill/qr/" + first["orderNo"])
    assert first["qrContent"].startswith("mockpay://order/")


def test_mock_pay_is_idempotent_and_credits_balance(seeded_org, logged_client):
    client = logged_client(seeded_org.admin_name, seeded_org.admin_password)
    order = _create_order(client, 12345)

    pending = client.get_recharge_order(order["orderNo"])["data"]
    assert pending["paid"] is False
    assert "balanceAfterCents" not in pending

    paid = client.mock_pay_order(order["orderNo"])["data"]
    assert paid["paid"] is True
    assert paid["balanceAfterCents"] == 12345

    # 重复到账不重复入账
    again = client.mock_pay_order(order["orderNo"])["data"]
    assert again["balanceAfterCents"] == 12345

    detail = client.get_recharge_order(order["orderNo"])["data"]
    assert detail["paid"] is True
    assert detail["balanceAfterCents"] == 12345

    txns = client.list_transactions()["data"]
    assert txns["total"] == 1
    item = txns["items"][0]
    assert item["type"] == 1
    assert item["amountCents"] == 12345
    assert item["balanceAfterCents"] == 12345

    assert client.list_transactions(type=2)["data"]["total"] == 0
    assert client.bootstrap()["data"]["balanceCents"] == 12345


def test_order_validation_errors(seeded_org, logged_client):
    client = logged_client(seeded_org.admin_name, seeded_org.admin_password)

    with pytest.raises(ApiError) as bad_amount:
        client.create_recharge_order(0)
    assert bad_amount.value.code == -903

    with pytest.raises(ApiError) as bad_channel:
        client.create_recharge_order(100, channel=5)
    assert bad_channel.value.code == -904

    with pytest.raises(ApiError) as missing:
        client.get_recharge_order("BO_NOT_EXIST")
    assert missing.value.code == -901


def test_order_is_private_to_owner(seeded_org, logged_client, base_url):
    owner = logged_client(seeded_org.admin_name, seeded_org.admin_password)
    order = _create_order(owner, 500)

    other = ApiClient(base_url)
    other.login_pwd(seeded_org.vendor_name, seeded_org.vendor_password)

    with pytest.raises(ApiError) as peek:
        other.get_recharge_order(order["orderNo"])
    assert peek.value.code == -901

    with pytest.raises(ApiError) as pay:
        other.mock_pay_order(order["orderNo"])
    assert pay.value.code == -901


def test_qr_placeholder_returns_png(seeded_org, logged_client):
    client = logged_client(seeded_org.admin_name, seeded_org.admin_password)
    order = _create_order(client, 100)

    resp = client.qr_raw(order["orderNo"])
    assert resp.status_code == 200
    assert "image/png" in resp.headers.get("Content-Type", "")
    assert resp.content[:8] == PNG_MAGIC

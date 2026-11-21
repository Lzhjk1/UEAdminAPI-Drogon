"""积分包 / 云工程空间在线购买。

渠道 9 为 TestMode 模拟支付；验证下单、结算发放、幂等、额度叠加与余额不变。
真实微信支付走同一结算路径，仅渠道下单不同。
"""
from __future__ import annotations

import uuid

from conftest import _psql, _sql_lit


def _set_subscription(team_id: int, plan: str = "plus"):
    _psql(f"delete from team_subscription where team_id = {team_id}")
    _psql(
        "insert into team_subscription(team_id, product, plan, start_date, end_date, status, is_trial) "
        f"values ({team_id}, 'ueaicad', {_sql_lit(plan)}, "
        "(now() at time zone 'Asia/Shanghai')::date - 1, "
        "(now() at time zone 'Asia/Shanghai')::date + 365, 1, false)"
    )


def _data(body: dict) -> dict:
    assert body.get("code") == 0, f"接口失败: {body}"
    return body.get("data") or {}


def _create_addon(client, sku: str, idem: str) -> dict:
    return _data(client._request(
        "POST", "/api/bill/addon",
        json_body={"sku": sku, "channel": 9, "idemKey": idem}))


def _mock_paid(client, order_no: str):
    return _data(client._request(
        "POST", f"/api/bill/order/{order_no}/mock-paid", json_body={}))


def test_credits_pack_paid_and_granted(seeded_org, logged_client):
    _set_subscription(seeded_org.team_id)
    client = logged_client(seeded_org.admin_name, seeded_org.admin_password)
    before = _psql(
        f"select balance_cents from user_balance where user_id = {seeded_org.admin_id}")[0]

    try:
        idem = "addon-" + uuid.uuid4().hex
        order = _create_addon(client, "credits_500", idem)
        assert order["amountCents"] == 10000
        assert order["productCode"] == "credits_500"
        order_no = order["orderNo"]

        _mock_paid(client, order_no)

        refund = client._request(
            "POST", f"/api/bill/order/{order_no}/refund",
            json_body={"reason": "测试"}, check=False)
        assert refund["code"] == -913, refund

        rows = _psql(
            "select kind, amount from team_addon "
            f"where team_id = {seeded_org.team_id} order by kind")
        assert rows == ["credits|500"], rows

        after = _psql(
            f"select balance_cents from user_balance where user_id = {seeded_org.admin_id}")[0]
        assert after == before, "加购不应增加余额"

        # 重复回调/重复模拟支付不重复发放
        _mock_paid(client, order_no)
        rows = _psql(
            f"select count(*) from team_addon where team_id = {seeded_org.team_id}")
        assert rows[0] == "1"

        usage = _data(client._request("GET", "/api/ai/usage"))
        assert usage["quota"]["monthlyTokens"] == 450000000  # 4 亿 + 500 积分
        assert usage["quota"]["monthlyCredits"] == 4500

        boot = _data(client.bootstrap())
        assert boot["addons"]["credits"] == 500
    finally:
        _psql(f"delete from team_addon where team_id = {seeded_org.team_id}")
        _psql(
            "delete from payment_order where user_id = "
            f"{seeded_org.admin_id} and product_code is not null")


def test_storage_pack_paid_and_granted(seeded_org, logged_client):
    _set_subscription(seeded_org.team_id)
    client = logged_client(seeded_org.admin_name, seeded_org.admin_password)

    try:
        order = _create_addon(client, "storage_100gb", "addon-" + uuid.uuid4().hex)
        assert order["amountCents"] == 49900
        _mock_paid(client, order["orderNo"])

        rows = _psql(
            "select kind, amount from team_addon "
            f"where team_id = {seeded_org.team_id} order by kind")
        assert rows == ["storage|100"], rows

        boot = _data(client.bootstrap())
        assert boot["addons"]["storageGb"] == 100
    finally:
        _psql(f"delete from team_addon where team_id = {seeded_org.team_id}")
        _psql(
            "delete from payment_order where user_id = "
            f"{seeded_org.admin_id} and product_code is not null")

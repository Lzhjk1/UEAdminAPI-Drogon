import pytest


def test_ping(client):
    body = client.ping()
    assert body["code"] == 0
    assert body["msg"] == "pong"
    # data.serverTime 应为整数时间戳
    assert isinstance(body.get("data", {}).get("serverTime"), int)
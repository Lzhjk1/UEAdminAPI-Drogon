"""内置 AI 月度积分额度闸门回归。

不调用真实模型上游：直接把本月用量写入 ai_usage_log，验证网关在转发前返回 -914。
"""
from __future__ import annotations

from api_client import ApiClient
from conftest import _psql, _sql_lit


def _set_subscription(team_id: int, product: str, plan: str, *, days: int = 365,
                      is_trial: bool = False):
    _psql(f"delete from team_subscription where team_id = {team_id}")
    _psql(
        "insert into team_subscription(team_id, product, plan, start_date, end_date, status, is_trial) "
        f"values ({team_id}, {_sql_lit(product)}, {_sql_lit(plan)}, "
        "(now() at time zone 'Asia/Shanghai')::date - 1, "
        f"(now() at time zone 'Asia/Shanghai')::date + {days}, 1, "
        f"{'true' if is_trial else 'false'})"
    )


def _seed_usage(team_id: int, user_id: int, tokens: int):
    _psql(
        "insert into ai_usage_log(user_id, team_id, model, prompt_tokens, "
        "completion_tokens, total_tokens) "
        f"values ({user_id}, {team_id}, 'test', {tokens}, 0, {tokens})"
    )


def _chat(client: ApiClient) -> dict:
    return client._request(
        "POST", "/api/ai/chat",
        json_body={"messages": [{"role": "user", "content": "ping"}]},
        check=False,
    )


def test_plus_quota_exceeded(seeded_org, logged_client):
    _set_subscription(seeded_org.team_id, "ueaicad", "plus")
    _seed_usage(seeded_org.team_id, seeded_org.admin_id, 400000000)
    client = logged_client(seeded_org.admin_name, seeded_org.admin_password)

    body = _chat(client)
    assert body["code"] == -914, body
    quota = body["data"]["quota"]
    assert quota["monthlyTokens"] == 400000000
    assert quota["monthlyCredits"] == 4000
    assert quota["usedTokens"] >= 400000000
    assert quota["remainingTokens"] == 0

    usage = client._request("GET", "/api/ai/usage", check=False)
    assert usage["code"] == 0, usage
    assert usage["data"]["quota"]["monthlyTokens"] == 400000000
    assert usage["data"]["quota"]["monthlyCredits"] == 4000
    assert usage["data"]["quota"]["usedTokens"] >= 400000000

    _psql(f"delete from ai_usage_log where team_id = {seeded_org.team_id}")


def test_trial_quota_exceeded(seeded_org, logged_client):
    _set_subscription(seeded_org.team_id, "ueaicad", "trial", days=5, is_trial=True)
    _seed_usage(seeded_org.team_id, seeded_org.admin_id, 200000000)
    client = logged_client(seeded_org.admin_name, seeded_org.admin_password)

    body = _chat(client)
    assert body["code"] == -914, body
    assert body["data"]["quota"]["monthlyTokens"] == 200000000
    assert body["data"]["quota"]["monthlyCredits"] == 2000


def test_free_tier_quota_exceeded(seeded_org, logged_client):
    _set_subscription(seeded_org.team_id, "ueaicad", "plus", days=-1)
    _seed_usage(seeded_org.team_id, seeded_org.admin_id, 10000000)
    client = logged_client(seeded_org.admin_name, seeded_org.admin_password)

    body = _chat(client)
    assert body["code"] == -914, body
    assert body["data"]["quota"]["monthlyTokens"] == 10000000
    assert body["data"]["quota"]["monthlyCredits"] == 100

    _psql(f"delete from ai_usage_log where team_id = {seeded_org.team_id}")

    _psql(f"delete from ai_usage_log where team_id = {seeded_org.team_id}")


def test_pdms_only_ai_denied(seeded_org, logged_client):
    _set_subscription(seeded_org.team_id, "pdms", "network")
    client = logged_client(seeded_org.admin_name, seeded_org.admin_password)

    body = _chat(client)
    assert body["code"] == -808, body
    assert "AutoPDMS" in body["msg"], body

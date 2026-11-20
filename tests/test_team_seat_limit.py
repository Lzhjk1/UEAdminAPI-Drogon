"""团队席位上限回归：Plus/试用 4 席、订阅到期仅 1 席。

新增成员必须被服务端拦截，不能只依赖界面禁用按钮。
依赖 conftest 的 seeded_org（直连数据库种入团队与管理员）和 psql。

运行前：
  UEADMINAPI_BASE_URL=http://127.0.0.1:6666
  UEADMINAPI_TEST_DB_URL=postgresql://ueadmin:ueadmin@127.0.0.1:5432/UEAdminAPI
"""
from __future__ import annotations

import uuid

import pytest

from api_client import ApiClient
from conftest import _psql, _sql_lit

MEMBER_PASSWORD = "Pwd@123456"
NAME_PREFIX = "seattest"


def _set_subscription(team_id: int, plan: str, active: bool):
    end_expr = "current_date + 365" if active else "current_date - 1"
    _psql(
        f"delete from team_subscription where team_id = {team_id}; "
        "insert into team_subscription(team_id, plan, start_date, end_date, status, is_trial) "
        f"values ({team_id}, {_sql_lit(plan)}, current_date - 30, {end_expr}, 1, false)"
    )


def _member_name(suffix: str, index: int) -> str:
    return f"{NAME_PREFIX}{suffix}_{index}"


def _create_member(client: ApiClient, name: str, check: bool = True):
    return client._request(
        "POST", "/api/team/member",
        json_body={"userName": name, "password": MEMBER_PASSWORD,
                   "roleCodes": ["engineer"], "nickName": name},
        check=check,
    )


def _ok(body: dict) -> dict:
    assert body.get("code") == 0, f"接口失败: {body}"
    return body.get("data") or {}


def _cleanup_created_members():
    """删除用例通过接口创建的成员；移除成员时生成的个人团队一并回收。"""
    users = f"""select id from "user" where name like '{NAME_PREFIX}%'"""
    try:
        _psql("delete from team_member_role where team_member_id in "
              f"(select id from team_member where user_id in ({users}))")
        _psql(f"delete from team_member where user_id in ({users})")
        _psql("delete from team_subscription where team_id in "
              f"(select id from team where owner_user_id in ({users}))")
        _psql("delete from team_feature_auth where team_id in "
              f"(select id from team where owner_user_id in ({users}))")
        _psql(f"delete from team where owner_user_id in ({users})")
        _psql(f"delete from user_balance where user_id in ({users})")
        _psql(f"""delete from "user" where name like '{NAME_PREFIX}%'""")
    except Exception as e:  # 清理失败不阻断用例结论，但留下排查线索
        print(f"[WARN] 席位用例清理失败: {e}")


@pytest.fixture
def plus_team(seeded_org):
    _set_subscription(seeded_org.team_id, "plus", True)
    yield seeded_org
    _cleanup_created_members()


def _fill_to_capacity(client: ApiClient, suffix: str, count: int) -> list[dict]:
    created = []
    for i in range(1, count + 1):
        created.append(_ok(_create_member(client, _member_name(suffix, i))))
    return created


def test_plus_rejects_fifth_member(plus_team, logged_client):
    admin = logged_client(plus_team.admin_name, plus_team.admin_password)
    suffix = uuid.uuid4().hex[:6]

    _fill_to_capacity(admin, suffix, 3)  # 管理员 + 3 = 4 席
    data = _ok(admin.team_info())
    assert len(data["members"]) == 4, data

    fifth = _create_member(admin, _member_name(suffix, 4), check=False)
    assert fifth["code"] != 0, fifth
    assert "席位已满" in fifth["msg"], fifth
    assert "4/4" in fifth["msg"], fifth

    # 被拒绝的账号不应被创建
    rows = _psql(f"""select count(*) from "user" where name = {_sql_lit(_member_name(suffix, 4))}""")
    assert rows[0] == "0"


def test_plus_seat_freed_after_remove(plus_team, logged_client):
    admin = logged_client(plus_team.admin_name, plus_team.admin_password)
    suffix = uuid.uuid4().hex[:6]

    created = _fill_to_capacity(admin, suffix, 3)
    denied = _create_member(admin, _member_name(suffix, 4), check=False)
    assert denied["code"] != 0 and "席位已满" in denied["msg"], denied

    _ok(admin._request("DELETE", f"/api/team/member/{created[0]['userId']}"))
    retry = _ok(_create_member(admin, _member_name(suffix, 4)))
    assert retry["userId"] > 0

    data = _ok(admin.team_info())
    assert len(data["members"]) == 4, data


def test_expired_subscription_blocks_addition(plus_team, logged_client):
    _set_subscription(plus_team.team_id, "plus", False)
    admin = logged_client(plus_team.admin_name, plus_team.admin_password)
    suffix = uuid.uuid4().hex[:6]

    denied = _create_member(admin, _member_name(suffix, 1), check=False)
    assert denied["code"] != 0, denied
    assert "席位已满" in denied["msg"], denied
    assert "1/1" in denied["msg"], denied

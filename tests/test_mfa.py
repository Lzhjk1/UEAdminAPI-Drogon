import uuid
import pytest
from api_client import ApiError
from conftest import fetch_code


def _gen_email(test_domain):
    return f"mfatest_{uuid.uuid4().hex[:8]}@{test_domain}"


def test_send_and_fetch_code(client, test_domain):
    """发送验证码后, 通过调试接口应能取回一个 6 位数字验证码"""
    email = _gen_email(test_domain)
    client.send_mfa(email, "Login")
    body = client.get_latest_code(email, "Login")
    assert body["code"] == 0
    code = body["data"]["code"]
    assert isinstance(code, str)
    assert len(code) == 6
    assert code.isdigit()


def test_check_code_correct(client, test_domain):
    """校验接口对真实生成的验证码应返回成功"""
    email = _gen_email(test_domain)
    code = fetch_code(client, email, "Login")
    body = client.check_mfa(email, code, "Login")
    assert body["code"] == 0
    assert "验证码正确" in body["msg"]


def test_check_code_wrong(client, test_domain):
    """任意错误码(与真实码不同的 6 位数字)应校验失败"""
    email = _gen_email(test_domain)
    client.send_mfa(email, "Login")
    # 取真实码后构造一个大概率不同的错误码
    real = client.get_latest_code(email, "Login")["data"]["code"]
    wrong = "000000" if real != "000000" else "111111"
    with pytest.raises(ApiError) as ei:
        client.check_mfa(email, wrong, "Login")
    assert ei.value.code == -401  # ApiError_InvalidVerifyCode


def test_check_code_not_consumed(client, test_domain):
    """check 接口 isConsume=false, 同一验证码应可重复校验通过"""
    email = _gen_email(test_domain)
    code = fetch_code(client, email, "Login")
    b1 = client.check_mfa(email, code, "Login")
    b2 = client.check_mfa(email, code, "Login")
    assert b1["code"] == 0 and b2["code"] == 0


def test_send_invalid_channel(client):
    """无法识别的 target(非邮箱非手机)应返回 -103 ApiError_InternalError"""
    with pytest.raises(ApiError) as ei:
        client.send_mfa("not-a-valid-target", "Login")
    assert ei.value.code == -103  # ApiError_InternalError


def test_latest_code_expired_or_absent(client, test_domain):
    """查询不存在的验证码应返回错误码"""
    email = _gen_email(test_domain)
    with pytest.raises(ApiError) as ei:
        client.get_latest_code(email, "Login")
    assert ei.value.code == -401


def test_code_is_random(client, test_domain):
    """验证码应为随机生成: 两次发送的码不应总是相同 (宽松断言, 仅排除极端退化)"""
    e1 = _gen_email(test_domain)
    e2 = _gen_email(test_domain)
    c1 = fetch_code(client, e1, "Login")
    c2 = fetch_code(client, e2, "Login")
    # 两个不同 target 各自的码可能恰好相同, 但连续 N 次都相同的概率极低
    codes = {c1, c2}
    for _ in range(4):
        e = _gen_email(test_domain)
        codes.add(fetch_code(client, e, "Login"))
    # 至少出现两种不同的码, 说明走的是随机生成而非硬编码常量
    assert len(codes) >= 2, "验证码未呈现随机性, 请确认 FixedCode 未生效或随机逻辑异常"
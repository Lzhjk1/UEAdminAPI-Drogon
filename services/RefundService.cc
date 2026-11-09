#include "RefundService.h"

#include <drogon/drogon.h>

#include "AlipayProvider.h"
#include "PaymentTypes.h"
#include "WechatPayProvider.h"
#include "utils/ApiErrorCodes.h"
#include "utils/PaymentConfig.h"

#include <algorithm>
#include <ctime>
#include <random>
#include <string>

using namespace drogon;
using namespace UEAdminAPI;
using namespace UEAdminAPI::utils;

namespace UEAdminAPI {
namespace Services {

namespace {

constexpr int kRefundWindowDays = 365;
constexpr size_t kMaxReasonBytes = 200;

std::string GenerateRefundNo()
{
    std::time_t now = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    char buf[32] = {0};
    std::strftime(buf, sizeof(buf), "RF%Y%m%d%H%M%S", &tmv);

    static std::mt19937 rng(static_cast<unsigned>(std::random_device{}()));
    std::uniform_int_distribution<int> dist(100000, 999999);
    return std::string(buf) + std::to_string(dist(rng));
}

std::string Trim(const std::string &text)
{
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
    {
        return "";
    }
    const auto end = text.find_last_not_of(" \t\r\n");
    std::string out = text.substr(begin, end - begin + 1);
    if (out.size() > kMaxReasonBytes)
    {
        out.resize(kMaxReasonBytes);
    }
    return out;
}

std::string RefundStatusText(int status)
{
    switch (status)
    {
    case 0:
        return "待审核";
    case 1:
        return "退款中";
    case 2:
        return "已退款";
    case 3:
        return "已驳回";
    case 4:
        return "退款失败";
    default:
        return "未知";
    }
}

Json::Value BuildRefundJson(const drogon::orm::Row &r, bool detail)
{
    Json::Value item;
    item["id"] = static_cast<Json::Int64>(r["id"].as<long long>());
    item["refundNo"] = r["refund_no"].as<std::string>();
    item["orderNo"] = r["order_no"].as<std::string>();
    item["userId"] = r["user_id"].as<int>();
    item["amountCents"] = static_cast<Json::Int64>(r["amount_cents"].as<int64_t>());
    item["reason"] = r["reason"].isNull() ? "" : r["reason"].as<std::string>();
    const int status = r["status"].as<int>();
    item["status"] = status;
    item["statusText"] = RefundStatusText(status);
    item["reviewRemark"] = r["review_remark"].isNull() ? "" : r["review_remark"].as<std::string>();
    item["errorMsg"] = r["error_msg"].isNull() ? "" : r["error_msg"].as<std::string>();
    item["createdAt"] = r["created_at"].isNull() ? "" : r["created_at"].as<std::string>();
    item["reviewedAt"] = r["reviewed_at"].isNull() ? "" : r["reviewed_at"].as<std::string>();
    item["refundedAt"] = r["refunded_at"].isNull() ? "" : r["refunded_at"].as<std::string>();
    item["providerRefundNo"] =
        r["provider_refund_no"].isNull() ? "" : r["provider_refund_no"].as<std::string>();
    item["providerState"] = r["provider_state"].isNull() ? "" : r["provider_state"].as<std::string>();
    item["userName"] = r["user_name"].isNull() ? "" : r["user_name"].as<std::string>();
    item["userEmail"] = r["user_email"].isNull() ? "" : r["user_email"].as<std::string>();

    if (detail)
    {
        item["channel"] = r["channel"].as<int>();
        item["orderAmountCents"] = static_cast<Json::Int64>(r["order_amount_cents"].as<int64_t>());
        item["orderPaidAt"] = r["order_paid_at"].isNull() ? "" : r["order_paid_at"].as<std::string>();
        item["providerTradeNo"] =
            r["provider_trade_no"].isNull() ? "" : r["provider_trade_no"].as<std::string>();
        item["reviewBy"] = r["review_by"].isNull() ? 0 : r["review_by"].as<int>();
    }
    return item;
}

const char *kAdminSelect =
    "select r.id, r.refund_no, r.order_no, r.user_id, r.amount_cents, r.reason, "
    "       r.status::int as status, r.review_remark, r.error_msg, "
    "       r.provider_refund_no, r.provider_state, r.review_by, "
    "       to_char(r.created_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD HH24:MI:SS') as created_at, "
    "       to_char(r.reviewed_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD HH24:MI:SS') as reviewed_at, "
    "       to_char(r.refunded_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD HH24:MI:SS') as refunded_at, "
    "       u.name as user_name, u.email as user_email, "
    "       o.channel::int as channel, o.amount_cents as order_amount_cents, o.provider_trade_no, "
    "       to_char(o.paid_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD HH24:MI:SS') as order_paid_at "
    "from payment_refund r "
    "join \"user\" u on u.id = r.user_id "
    "join payment_order o on o.order_no = r.order_no ";

// 审批失败时回滚已扣余额：恢复余额 + 记一笔冲正流水 + 退款单置为失败
bool RollbackRefund(long long id, const std::string &errorMsg)
{
    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync(
            "with r as ("
            "    select id, user_id, amount_cents from payment_refund "
            "    where id = $1 and status = 1 for update"
            "), bal as ("
            "    update user_balance ub set balance_cents = ub.balance_cents + r.amount_cents, updated_at = now() "
            "    from r where ub.user_id = r.user_id "
            "    returning ub.user_id, ub.balance_cents, r.id as refund_id, r.amount_cents"
            "), tx as ("
            "    insert into balance_txn (user_id, type, amount_cents, balance_after, ext_ref, remark) "
            "    select user_id, 1, amount_cents, balance_cents, "
            "           'refund-rollback:' || refund_id || ':' || to_char(clock_timestamp(), 'YYYYMMDDHH24MISSUS'), "
            "           '退款失败冲正' "
            "    from bal returning id"
            "), upd as ("
            "    update payment_refund pr set status = 4, error_msg = $2, updated_at = now() "
            "    from bal where pr.id = bal.refund_id returning pr.id"
            ") select count(*) as n from upd",
            id, errorMsg);
        return !rows.empty() && rows[0]["n"].as<long long>() > 0;
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "[Refund] 回滚失败, 需人工核对 refundId=" << id << ": " << e.base().what();
        return false;
    }
}

} // namespace

std::string RefundService::StatusText(int status)
{
    return RefundStatusText(status);
}

HttpResult RefundService::Apply(int userId, const std::string &orderNo, const std::string &reason)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        auto orderRows = db->execSqlSync(
            "select order_no, amount_cents, status::int as status, "
            "       (paid_at is not null and paid_at < now() - ($3::int * interval '1 day')) as too_old "
            "from payment_order where order_no = $1 and user_id = $2",
            orderNo, userId, kRefundWindowDays);
        if (orderRows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_OrderNotFound);
            return result;
        }
        const auto &order = orderRows[0];
        if (order["status"].as<int>() != 1)
        {
            result.setResult(ApiErrorCode::ApiError_RefundNotAllowed, "订单未支付，不能退款");
            return result;
        }
        if (order["too_old"].as<bool>())
        {
            result.setResult(ApiErrorCode::ApiError_RefundNotAllowed, "超过可退款期限（365 天）");
            return result;
        }
        const int64_t amountCents = order["amount_cents"].as<int64_t>();

        auto active = db->execSqlSync(
            "select status::int as status from payment_refund "
            "where order_no = $1 and status in (0, 1, 2) limit 1",
            orderNo);
        if (!active.empty())
        {
            const int st = active[0]["status"].as<int>();
            result.setResult(ApiErrorCode::ApiError_RefundNotAllowed,
                             "该订单已有退款申请（" + RefundStatusText(st) + "），不能重复申请");
            return result;
        }

        auto bal = db->execSqlSync("select balance_cents from user_balance where user_id = $1", userId);
        const int64_t balance = bal.empty() ? 0 : bal[0]["balance_cents"].as<int64_t>();
        if (balance < amountCents)
        {
            result.setResult(ApiErrorCode::ApiError_BalanceInsufficient,
                             "当前余额不足以整单退款（余额 " + std::to_string(balance) +
                                 " 分，订单 " + std::to_string(amountCents) + " 分）");
            return result;
        }

        const std::string refundNo = GenerateRefundNo();
        auto inserted = db->execSqlSync(
            "insert into payment_refund (refund_no, order_no, user_id, amount_cents, reason, status) "
            "values ($1, $2, $3, $4, nullif($5, ''), 0) "
            "returning id, "
            "to_char(created_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD HH24:MI:SS') as created_at",
            refundNo, orderNo, userId, amountCents, Trim(reason));

        result.jsondata["id"] = static_cast<Json::Int64>(inserted[0]["id"].as<long long>());
        result.jsondata["refundNo"] = refundNo;
        result.jsondata["orderNo"] = orderNo;
        result.jsondata["amountCents"] = static_cast<Json::Int64>(amountCents);
        result.jsondata["status"] = 0;
        result.jsondata["statusText"] = RefundStatusText(0);
        result.jsondata["createdAt"] = inserted[0]["created_at"].as<std::string>();
        result.setResult(ApiErrorCode::ApiError_Success, "退款申请已提交，等待审核");
        LOG_INFO << "[Refund] 用户提交退款申请 userId=" << userId << " orderNo=" << orderNo
                 << " refundNo=" << refundNo << " amount=" << amountCents;
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        const std::string what = e.base().what();
        if (what.find("uq_payment_refund_active") != std::string::npos)
        {
            result.setResult(ApiErrorCode::ApiError_RefundNotAllowed, "该订单已有退款申请，不能重复申请");
            return result;
        }
        LOG_ERROR << "[Refund] 提交申请失败: " << what;
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult RefundService::AdminList(int status, const std::string &keyword, int page, int size)
{
    HttpResult result;
    if (page <= 0)
    {
        page = 1;
    }
    if (size <= 0 || size > 200)
    {
        size = 50;
    }

    auto db = drogon::app().getDbClient();
    try
    {
        auto countRows = db->execSqlSync(
            "select count(*) as total from payment_refund r "
            "join \"user\" u on u.id = r.user_id "
            "join payment_order o on o.order_no = r.order_no "
            "where ($1::int = -1 or r.status::int = $1::int) "
            "  and (coalesce($2, '') = '' or r.order_no ilike '%' || $2 || '%' "
            "       or r.refund_no ilike '%' || $2 || '%' "
            "       or u.name ilike '%' || $2 || '%' "
            "       or coalesce(u.email, '') ilike '%' || $2 || '%')",
            status, keyword);
        const int64_t total = countRows.empty() ? 0 : countRows[0]["total"].as<int64_t>();

        auto rows = db->execSqlSync(
            std::string(kAdminSelect) +
                "where ($1::int = -1 or r.status::int = $1::int) "
                "  and (coalesce($2, '') = '' or r.order_no ilike '%' || $2 || '%' "
                "       or r.refund_no ilike '%' || $2 || '%' "
                "       or u.name ilike '%' || $2 || '%' "
                "       or coalesce(u.email, '') ilike '%' || $2 || '%') "
                "order by r.id desc limit $3::int offset $4::int",
            status, keyword, size, (page - 1) * size);

        Json::Value items(Json::arrayValue);
        for (const auto &r : rows)
        {
            items.append(BuildRefundJson(r, false));
        }
        result.jsondata["total"] = static_cast<Json::Int64>(total);
        result.jsondata["page"] = page;
        result.jsondata["size"] = size;
        result.jsondata["items"] = items;
        result.setResult(ApiErrorCode::ApiError_Success, "success");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "[Refund] 列表失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult RefundService::AdminGet(long long id)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync(std::string(kAdminSelect) + "where r.id = $1", id);
        if (rows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_RefundNotFound);
            return result;
        }
        result.jsondata = BuildRefundJson(rows[0], true);
        result.setResult(ApiErrorCode::ApiError_Success, "success");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "[Refund] 详情失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

Task<HttpResult> RefundService::AdminApprove(int adminUserId, long long id, const std::string &remark)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();

    std::string refundNo;
    std::string orderNo;
    int64_t amountCents = 0;
    int64_t orderAmountCents = 0;
    int channel = 0;

    try
    {
        auto rows = db->execSqlSync(
            "select r.refund_no, r.order_no, r.amount_cents, r.status::int as status, r.reason, "
            "       o.channel::int as channel, o.amount_cents as order_amount_cents "
            "from payment_refund r join payment_order o on o.order_no = r.order_no where r.id = $1",
            id);
        if (rows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_RefundNotFound);
            co_return result;
        }
        const auto &r = rows[0];
        const int status = r["status"].as<int>();
        if (status != 0 && status != 4)
        {
            result.setResult(ApiErrorCode::ApiError_RefundStateInvalid,
                             "当前状态为「" + RefundStatusText(status) + "」，不能审批");
            co_return result;
        }
        refundNo = r["refund_no"].as<std::string>();
        orderNo = r["order_no"].as<std::string>();
        amountCents = r["amount_cents"].as<int64_t>();
        orderAmountCents = r["order_amount_cents"].as<int64_t>();
        channel = r["channel"].as<int>();

        // 先扣余额并把退款单置为「退款中」；余额不足时这里不会有任何行返回
        auto locked = db->execSqlSync(
            "with r as ("
            "    select id, user_id, amount_cents from payment_refund "
            "    where id = $1 and status in (0, 4) for update"
            "), bal as ("
            "    update user_balance ub set balance_cents = ub.balance_cents - r.amount_cents, updated_at = now() "
            "    from r where ub.user_id = r.user_id and ub.balance_cents >= r.amount_cents "
            "    returning ub.user_id, ub.balance_cents, r.id as refund_id, r.amount_cents"
            "), tx as ("
            "    insert into balance_txn (user_id, type, amount_cents, balance_after, ext_ref, remark) "
            "    select user_id, 3, -amount_cents, balance_cents, "
            "           'refund:' || refund_id || ':' || to_char(clock_timestamp(), 'YYYYMMDDHH24MISSUS'), '退款' "
            "    from bal returning id"
            "), upd as ("
            "    update payment_refund pr set status = 1, review_by = $2, review_remark = nullif($3, ''), "
            "           reviewed_at = now(), updated_at = now(), error_msg = null "
            "    from bal where pr.id = bal.refund_id returning pr.id"
            ") select bal.refund_id, bal.balance_cents from bal",
            id, adminUserId, Trim(remark));

        if (locked.empty())
        {
            auto chk = db->execSqlSync("select status::int as status from payment_refund where id = $1", id);
            if (chk.empty())
            {
                result.setResult(ApiErrorCode::ApiError_RefundNotFound);
            }
            else
            {
                const int st = chk[0]["status"].as<int>();
                if (st != 0 && st != 4)
                {
                    result.setResult(ApiErrorCode::ApiError_RefundStateInvalid,
                                     "当前状态为「" + RefundStatusText(st) + "」，不能审批");
                }
                else
                {
                    result.setResult(ApiErrorCode::ApiError_BalanceInsufficient,
                                     "用户当前余额不足，无法整单退款");
                }
            }
            co_return result;
        }

        const int64_t balanceAfter = locked[0]["balance_cents"].as<int64_t>();

        PayRefundResult refund;
        if (channel == 1)
        {
            refund = co_await AlipayProvider::Refund(orderNo, refundNo, amountCents, "余额充值退款");
        }
        else if (channel == 2)
        {
            refund = co_await WechatPayProvider::Refund(orderNo, refundNo, amountCents,
                                                        orderAmountCents, "余额充值退款");
        }
        else
        {
            RollbackRefund(id, "不支持的支付渠道");
            result.setResult(ApiErrorCode::ApiError_RefundProviderError, "该订单渠道不支持退款");
            co_return result;
        }

        if (!refund.ok || (!refund.success && !refund.processing))
        {
            const std::string msg = refund.error.empty() ? "渠道退款失败" : refund.error;
            RollbackRefund(id, msg);
            result.setResult(ApiErrorCode::ApiError_RefundProviderError, msg);
            co_return result;
        }

        if (refund.success)
        {
            db->execSqlSync(
                "update payment_refund set status = 2, provider_refund_no = nullif($2, ''), "
                "       provider_state = nullif($3, ''), error_msg = null, "
                "       refunded_at = now(), updated_at = now() "
                "where id = $1 and status = 1",
                id, refund.providerRefundNo, refund.state);
        }
        else
        {
            db->execSqlSync(
                "update payment_refund set provider_refund_no = nullif($2, ''), "
                "       provider_state = nullif($3, ''), updated_at = now() "
                "where id = $1 and status = 1",
                id, refund.providerRefundNo, refund.state);
        }

        const int newStatus = refund.success ? 2 : 1;
        result.jsondata["id"] = static_cast<Json::Int64>(id);
        result.jsondata["refundNo"] = refundNo;
        result.jsondata["orderNo"] = orderNo;
        result.jsondata["status"] = newStatus;
        result.jsondata["statusText"] = RefundStatusText(newStatus);
        result.jsondata["balanceAfterCents"] = static_cast<Json::Int64>(balanceAfter);
        result.setResult(ApiErrorCode::ApiError_Success,
                         refund.success ? "退款成功" : "退款已受理，渠道处理中");
        LOG_INFO << "[Refund] 审批通过 admin=" << adminUserId << " refundNo=" << refundNo
                 << " orderNo=" << orderNo << " amount=" << amountCents
                 << " providerStatus=" << refund.state;
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "[Refund] 审批异常: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    co_return result;
}

HttpResult RefundService::AdminReject(int adminUserId, long long id, const std::string &remark)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync(
            "update payment_refund set status = 3, review_by = $2, review_remark = nullif($3, ''), "
            "       reviewed_at = now(), updated_at = now(), error_msg = null "
            "where id = $1 and status in (0, 4) returning id",
            id, adminUserId, Trim(remark));
        if (rows.empty())
        {
            auto chk = db->execSqlSync("select status::int as status from payment_refund where id = $1", id);
            if (chk.empty())
            {
                result.setResult(ApiErrorCode::ApiError_RefundNotFound);
            }
            else
            {
                const int st = chk[0]["status"].as<int>();
                result.setResult(ApiErrorCode::ApiError_RefundStateInvalid,
                                 "当前状态为「" + RefundStatusText(st) + "」，不能驳回");
            }
            return result;
        }
        result.jsondata["id"] = static_cast<Json::Int64>(id);
        result.jsondata["status"] = 3;
        result.jsondata["statusText"] = RefundStatusText(3);
        result.setResult(ApiErrorCode::ApiError_Success, "已驳回");
        LOG_INFO << "[Refund] 驳回 admin=" << adminUserId << " refundId=" << id;
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "[Refund] 驳回失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

Task<HttpResult> RefundService::AdminSync(long long id)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();

    std::string refundNo;
    std::string orderNo;
    int channel = 0;
    try
    {
        auto rows = db->execSqlSync(
            "select r.refund_no, r.order_no, r.status::int as status, o.channel::int as channel "
            "from payment_refund r join payment_order o on o.order_no = r.order_no where r.id = $1",
            id);
        if (rows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_RefundNotFound);
            co_return result;
        }
        const int status = rows[0]["status"].as<int>();
        if (status != 1)
        {
            result.setResult(ApiErrorCode::ApiError_RefundStateInvalid,
                             "当前状态为「" + RefundStatusText(status) + "」，无需同步");
            co_return result;
        }
        refundNo = rows[0]["refund_no"].as<std::string>();
        orderNo = rows[0]["order_no"].as<std::string>();
        channel = rows[0]["channel"].as<int>();

        PayRefundResult refund;
        if (channel == 1)
        {
            refund = co_await AlipayProvider::QueryRefund(orderNo, refundNo);
        }
        else if (channel == 2)
        {
            refund = co_await WechatPayProvider::QueryRefund(refundNo);
        }
        else
        {
            result.setResult(ApiErrorCode::ApiError_RefundProviderError, "该订单渠道不支持退款查询");
            co_return result;
        }

        if (!refund.ok)
        {
            result.setResult(ApiErrorCode::ApiError_RefundProviderError,
                             refund.error.empty() ? "渠道退款查询失败" : refund.error);
            co_return result;
        }

        if (refund.success)
        {
            db->execSqlSync(
                "update payment_refund set status = 2, provider_refund_no = nullif($2, ''), "
                "       provider_state = nullif($3, ''), error_msg = null, "
                "       refunded_at = now(), updated_at = now() "
                "where id = $1 and status = 1",
                id, refund.providerRefundNo, refund.state);
            result.jsondata["status"] = 2;
            result.jsondata["statusText"] = RefundStatusText(2);
            result.setResult(ApiErrorCode::ApiError_Success, "退款已完成");
        }
        else if (refund.processing)
        {
            db->execSqlSync(
                "update payment_refund set provider_refund_no = coalesce(nullif($2, ''), provider_refund_no), "
                "       provider_state = nullif($3, ''), updated_at = now() "
                "where id = $1 and status = 1",
                id, refund.providerRefundNo, refund.state);
            result.jsondata["status"] = 1;
            result.jsondata["statusText"] = RefundStatusText(1);
            result.setResult(ApiErrorCode::ApiError_Success, "渠道仍在处理中");
        }
        else
        {
            // 渠道明确失败/关闭：回滚余额，退款单置为失败，允许重新申请或重新审批
            const std::string msg = refund.state.empty() ? "渠道退款失败" : ("渠道状态: " + refund.state);
            RollbackRefund(id, msg);
            result.jsondata["status"] = 4;
            result.jsondata["statusText"] = RefundStatusText(4);
            result.setResult(ApiErrorCode::ApiError_RefundProviderError, msg);
        }
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "[Refund] 同步异常: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    co_return result;
}

drogon::Task<HttpResult> RefundService::AdminRollback(int adminUserId, long long id)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();

    std::string refundNo;
    std::string orderNo;
    int channel = 0;
    try
    {
        auto rows = db->execSqlSync(
            "select r.status::int as status, r.refund_no, r.order_no, o.channel::int as channel "
            "from payment_refund r join payment_order o on o.order_no = r.order_no where r.id = $1",
            id);
        if (rows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_RefundNotFound);
            co_return result;
        }
        if (rows[0]["status"].as<int>() != 1)
        {
            result.setResult(ApiErrorCode::ApiError_RefundStateInvalid, "只有「退款中」的申请可以回滚");
            co_return result;
        }
        refundNo = rows[0]["refund_no"].as<std::string>();
        orderNo = rows[0]["order_no"].as<std::string>();
        channel = rows[0]["channel"].as<int>();
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "[Refund] 回滚查询失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
        co_return result;
    }

    // 手动回滚前必须向渠道确认“确实没退成功”，否则会造成余额与渠道双重退款。
    PayRefundResult refund;
    if (channel == 1)
    {
        refund = co_await AlipayProvider::QueryRefund(orderNo, refundNo);
    }
    else if (channel == 2)
    {
        refund = co_await WechatPayProvider::QueryRefund(refundNo);
    }
    else
    {
        result.setResult(ApiErrorCode::ApiError_RefundProviderError, "该订单渠道不支持退款查询");
        co_return result;
    }

    if (!refund.ok)
    {
        result.setResult(ApiErrorCode::ApiError_RefundProviderError,
                         refund.error.empty() ? "渠道退款查询失败，不能确认状态" : refund.error);
        co_return result;
    }
    if (refund.success)
    {
        db->execSqlSync(
            "update payment_refund set status = 2, provider_refund_no = nullif($2, ''), "
            "       provider_state = nullif($3, ''), error_msg = null, "
            "       refunded_at = coalesce(refunded_at, now()), updated_at = now() "
            "where id = $1 and status = 1",
            id, refund.providerRefundNo, refund.state);
        result.setResult(ApiErrorCode::ApiError_RefundStateInvalid,
                         "渠道已退款成功，不能冲正余额");
        co_return result;
    }
    if (refund.processing)
    {
        result.setResult(ApiErrorCode::ApiError_RefundStateInvalid,
                         "渠道仍在处理中，请稍后同步，不能回滚");
        co_return result;
    }

    try
    {
        if (!RollbackRefund(id, "管理员手动回滚"))
        {
            result.setResult(ApiErrorCode::ApiError_DatabaseError, "回滚失败，请检查日志");
            co_return result;
        }

        db->execSqlSync(
            "update payment_refund set review_by = $2, review_remark = '管理员手动回滚', "
            "       reviewed_at = now(), updated_at = now() where id = $1",
            id, adminUserId);

        result.jsondata["id"] = static_cast<Json::Int64>(id);
        result.jsondata["status"] = 4;
        result.jsondata["statusText"] = RefundStatusText(4);
        result.setResult(ApiErrorCode::ApiError_Success, "已回滚余额，退款单标记为失败");
        LOG_WARN << "[Refund] 管理员手动回滚 admin=" << adminUserId << " refundId=" << id;
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "[Refund] 回滚异常: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    co_return result;
}

} // namespace Services
} // namespace UEAdminAPI

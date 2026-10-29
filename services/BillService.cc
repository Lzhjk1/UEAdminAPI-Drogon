#include "BillService.h"

#include <drogon/drogon.h>

#include "AlipayProvider.h"
#include "PaymentTypes.h"
#include "RefundService.h"
#include "WechatPayProvider.h"
#include "utils/ApiErrorCodes.h"
#include "utils/PaymentConfig.h"
#include "utils/QrPng.h"
#include "utils/PaymentUtils.h"

#include <chrono>
#include <ctime>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <unordered_map>

using namespace drogon;
using namespace UEAdminAPI;
using namespace UEAdminAPI::utils;

namespace UEAdminAPI {
namespace Services {

namespace {

constexpr int64_t kMaxAmountCents = 10000000;  // 单笔上限 10 万元
constexpr int kOrderExpireMinutes = 30;
constexpr int kQueryThrottleSeconds = 4;

bool IsTestMode()
{
    const auto &cfg = drogon::app().getCustomConfig();
    if (!cfg.isMember("TestMode") || !cfg["TestMode"].isObject())
    {
        return false;
    }
    return cfg["TestMode"].get("Enable", false).asBool();
}

std::string GenerateOrderNo()
{
    std::time_t now = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    char buf[32] = {0};
    std::strftime(buf, sizeof(buf), "BO%Y%m%d%H%M%S", &tmv);

    static std::mt19937 rng(static_cast<unsigned>(std::random_device{}()));
    std::uniform_int_distribution<int> dist(100000, 999999);
    return std::string(buf) + std::to_string(dist(rng));
}

std::string BuildQrUrl(const std::string &orderNo)
{
    const auto &cfg = drogon::app().getCustomConfig();
    std::string host = cfg.get("ServerHost", "").asString();
    // 去掉末尾斜杠，避免出现 //
    while (!host.empty() && host.back() == '/')
    {
        host.pop_back();
    }
    return host + "/api/bill/qr/" + orderNo;
}

bool ShouldQueryProvider(const std::string &orderNo)
{
    static std::mutex mutex;
    static std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastQuery;

    std::lock_guard<std::mutex> lock(mutex);
    const auto now = std::chrono::steady_clock::now();
    const auto it = lastQuery.find(orderNo);
    if (it != lastQuery.end() &&
        now - it->second < std::chrono::seconds(kQueryThrottleSeconds))
    {
        return false;
    }
    if (lastQuery.size() > 20000)
    {
        lastQuery.clear();
    }
    lastQuery[orderNo] = now;
    return true;
}

Task<PayCreateResult> CreateProviderOrder(int channel, const std::string &orderNo, int64_t amountCents)
{
    if (channel == 1)
    {
        co_return co_await AlipayProvider::CreateOrder(orderNo, amountCents, "AutoPDMS 余额充值");
    }
    if (channel == 2)
    {
        co_return co_await WechatPayProvider::CreateNative(orderNo, amountCents, "AutoPDMS 余额充值");
    }
    PayCreateResult result;
    result.error = "不支持的支付渠道";
    co_return result;
}

Task<PayQueryResult> QueryProvider(int channel, const std::string &orderNo)
{
    if (channel == 1)
    {
        co_return co_await AlipayProvider::Query(orderNo);
    }
    if (channel == 2)
    {
        co_return co_await WechatPayProvider::Query(orderNo);
    }
    PayQueryResult result;
    result.error = "不支持的支付渠道";
    co_return result;
}

// 幂等结算：校验订单/渠道/金额后，一条 CTE 完成「置为已支付 + 建余额行 + 加余额 + 记流水」。
// 回调与主动查单都走这里，out_trade_no 唯一 + update ... where status = 0 保证并发安全。
bool SettleOrder(const std::string &orderNo,
                 int channel,
                 const std::string &providerTradeNo,
                 int64_t paidCents,
                 const char *source,
                 std::string &error)
{
    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync(
            "select user_id, amount_cents, channel::int as channel, status::int as status "
            "from payment_order where order_no = $1",
            orderNo);
        if (rows.empty())
        {
            error = "订单不存在";
            LOG_WARN << "[Payment][" << source << "] 结算失败: orderNo=" << orderNo << " 不存在";
            return false;
        }
        const auto &r = rows[0];
        const int orderChannel = r["channel"].as<int>();
        const int status = r["status"].as<int>();
        const int64_t amountCents = r["amount_cents"].as<int64_t>();

        if (orderChannel != channel)
        {
            error = "支付渠道不匹配";
            LOG_ERROR << "[Payment][" << source << "] 结算失败: orderNo=" << orderNo << " 渠道不一致";
            return false;
        }
        if (paidCents != amountCents)
        {
            error = "支付金额与订单不一致";
            LOG_ERROR << "[Payment][" << source << "] 结算失败: orderNo=" << orderNo
                      << " 实付=" << paidCents << " 应付=" << amountCents;
            return false;
        }
        if (status == 1)
        {
            error.clear();
            return true;
        }
        if (status != 0)
        {
            error = "订单状态不允许结算";
            LOG_WARN << "[Payment][" << source << "] 结算失败: orderNo=" << orderNo << " status=" << status;
            return false;
        }

        const std::string extRef = "order:" + orderNo;
        auto settled = db->execSqlSync(
            "with o as ("
            "    update payment_order set status = 1, paid_at = now(), "
            "        provider_trade_no = coalesce(nullif($2, ''), provider_trade_no) "
            "    where order_no = $1 and status = 0 "
            "    returning id, user_id, amount_cents"
            "), ensured as ("
            "    insert into user_balance (user_id) select user_id from o "
            "    on conflict (user_id) do nothing returning user_id"
            "), up as ("
            "    update user_balance ub set balance_cents = ub.balance_cents + o.amount_cents, updated_at = now() "
            "    from o where ub.user_id = o.user_id "
            "    returning ub.user_id, ub.balance_cents, o.amount_cents, o.id as order_id"
            "), txn as ("
            "    insert into balance_txn (user_id, type, amount_cents, balance_after, order_id, ext_ref, remark) "
            "    select up.user_id, 1, up.amount_cents, up.balance_cents, up.order_id, $3, '充值' from up "
            "    returning id"
            ") select up.balance_cents from up",
            orderNo, providerTradeNo, extRef);

        if (settled.empty())
        {
            // 并发竞争：另一个请求已完成入账
            error.clear();
            return true;
        }
        LOG_INFO << "[Payment][" << source << "] 入账成功 orderNo=" << orderNo
                 << " amount=" << amountCents
                 << " balance=" << settled[0]["balance_cents"].as<int64_t>()
                 << " tradeNo=" << providerTradeNo;
        error.clear();
        return true;
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        error = "数据库错误";
        LOG_ERROR << "[Payment][" << source << "] 结算异常: " << e.base().what();
        return false;
    }
}

void LogCallback(const std::string &orderNo,
                 int channel,
                 const std::string &body,
                 const std::map<std::string, std::string> &headers,
                 bool verified)
{
    auto db = drogon::app().getDbClient();
    try
    {
        Json::Value headerJson(Json::objectValue);
        for (const auto &[key, value] : headers)
        {
            headerJson[key] = value;
        }
        db->execSqlSync(
            "insert into payment_callback_log (order_no, channel, raw_body, headers, verified) "
            "values (nullif($1, ''), $2::int, $3, $4::jsonb, $5)",
            orderNo,
            channel,
            body.substr(0, 65535),
            PaymentUtils::JsonCompact(headerJson),
            verified);
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        // 回调日志失败不影响验签与入账，只记录一条错误
        LOG_ERROR << "[Payment] 写回调日志失败: " << e.base().what();
    }
}

std::string MockQrContent(const std::string &orderNo)
{
    return "mockpay://order/" + orderNo;
}

bool ShouldUseMock(int channel)
{
    if (channel == 9)
    {
        return true;
    }
    // 真实渠道未配置时，仅在 TestMode 下退回模拟二维码，避免开发环境把真实渠道误当可用
    return !PaymentConfig::ChannelReady(channel) && IsTestMode();
}

} // namespace

Task<HttpResult> BillService::CreateOrder(int userId, int64_t amountCents, int channel, const std::string &idemKey)
{
    HttpResult result;
    if (amountCents <= 0 || amountCents > kMaxAmountCents)
    {
        result.setResult(ApiErrorCode::ApiError_AmountInvalid);
        co_return result;
    }
    if (channel != 1 && channel != 2 && channel != 9)
    {
        result.setResult(ApiErrorCode::ApiError_PayChannelUnsupported);
        co_return result;
    }

    auto db = drogon::app().getDbClient();
    try
    {
        // 幂等：同一 idemKey 直接返回原订单
        if (!idemKey.empty())
        {
            auto exist = db->execSqlSync(
                "select order_no, amount_cents, channel::int as channel, status::int as status, "
                "       qr_content, pay_url, "
                "       (expire_at is not null and expire_at < now()) as expired, "
                "       to_char(expire_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD HH24:MI:SS') as expire_at "
                "from payment_order where user_id = $1 and idem_key = $2",
                userId, idemKey);
            if (!exist.empty())
            {
                const auto &r = exist[0];
                const std::string orderNo = r["order_no"].as<std::string>();
                const int status = r["status"].as<int>();
                const bool expired = r["expired"].as<bool>();
                std::string qrContent = r["qr_content"].isNull() ? "" : r["qr_content"].as<std::string>();
                std::string payUrl = r["pay_url"].isNull() ? "" : r["pay_url"].as<std::string>();

                if (status == 2 || (status == 0 && expired))
                {
                    result.setResult(ApiErrorCode::ApiError_OrderStateInvalid, "订单已关闭或已过期，请重新下单");
                    co_return result;
                }

                if (status == 0 && qrContent.empty() && payUrl.empty())
                {
                    if (ShouldUseMock(channel))
                    {
                        qrContent = MockQrContent(orderNo);
                    }
                    else if (!PaymentConfig::ChannelReady(channel))
                    {
                        result.setResult(ApiErrorCode::ApiError_PayProviderError,
                                         std::string("支付渠道未配置: ") + (channel == 1 ? "支付宝" : "微信支付"));
                        co_return result;
                    }
                    else
                    {
                        PayCreateResult created = co_await CreateProviderOrder(channel, orderNo, amountCents);
                        if (!created.ok)
                        {
                            db->execSqlSync("update payment_order set status = 2 where order_no = $1 and status = 0",
                                            orderNo);
                            result.setResult(ApiErrorCode::ApiError_PayProviderError, created.error);
                            co_return result;
                        }
                        qrContent = created.qrContent;
                        payUrl = created.payUrl;
                        db->execSqlSync(
                            "update payment_order set qr_content = nullif($2, ''), pay_url = nullif($3, ''), "
                            "       provider_trade_no = coalesce(nullif($4, ''), provider_trade_no) "
                            "where order_no = $1",
                            orderNo, qrContent, payUrl, created.providerTradeNo);
                    }
                    if (!qrContent.empty() && ShouldUseMock(channel))
                    {
                        db->execSqlSync("update payment_order set qr_content = $2 where order_no = $1",
                                        orderNo, qrContent);
                    }
                }

                result.jsondata["orderNo"] = orderNo;
                result.jsondata["amountCents"] = static_cast<Json::Int64>(r["amount_cents"].as<int64_t>());
                result.jsondata["channel"] = r["channel"].as<int>();
                result.jsondata["status"] = status;
                result.jsondata["expireAt"] = r["expire_at"].isNull() ? "" : r["expire_at"].as<std::string>();
                result.jsondata["qrContent"] = qrContent;
                result.jsondata["payUrl"] = payUrl;
                result.jsondata["qrPngUrl"] = BuildQrUrl(orderNo);
                result.setResult(ApiErrorCode::ApiError_Success, "order exists");
                co_return result;
            }
        }

        const std::string orderNo = GenerateOrderNo();
        const std::string idemValue = idemKey.empty() ? orderNo : idemKey;

        auto rows = db->execSqlSync(
            "insert into payment_order (order_no, user_id, amount_cents, channel, status, idem_key, expire_at) "
            "values ($1, $2, $3, $4::int, 0, $5, now() + ($6::int * interval '1 minute')) "
            "returning to_char(expire_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD HH24:MI:SS') as expire_at",
            orderNo, userId, amountCents, channel, idemValue, std::to_string(kOrderExpireMinutes));

        std::string qrContent;
        std::string payUrl;
        if (ShouldUseMock(channel))
        {
            qrContent = MockQrContent(orderNo);
        }
        else if (!PaymentConfig::ChannelReady(channel))
        {
            db->execSqlSync("update payment_order set status = 2 where order_no = $1 and status = 0", orderNo);
            result.setResult(ApiErrorCode::ApiError_PayProviderError,
                             std::string("支付渠道未配置: ") + (channel == 1 ? "支付宝" : "微信支付"));
            co_return result;
        }
        else
        {
            PayCreateResult created = co_await CreateProviderOrder(channel, orderNo, amountCents);
            if (!created.ok)
            {
                db->execSqlSync("update payment_order set status = 2 where order_no = $1 and status = 0", orderNo);
                result.setResult(ApiErrorCode::ApiError_PayProviderError, created.error);
                co_return result;
            }
            qrContent = created.qrContent;
            payUrl = created.payUrl;
            db->execSqlSync(
                "update payment_order set qr_content = nullif($2, ''), pay_url = nullif($3, ''), "
                "       provider_trade_no = coalesce(nullif($4, ''), provider_trade_no) "
                "where order_no = $1",
                orderNo, qrContent, payUrl, created.providerTradeNo);
        }

        if (ShouldUseMock(channel))
        {
            db->execSqlSync("update payment_order set qr_content = $2 where order_no = $1", orderNo, qrContent);
        }

        result.jsondata["orderNo"] = orderNo;
        result.jsondata["amountCents"] = static_cast<Json::Int64>(amountCents);
        result.jsondata["channel"] = channel;
        result.jsondata["status"] = 0;
        result.jsondata["qrContent"] = qrContent;
        result.jsondata["payUrl"] = payUrl;
        result.jsondata["qrPngUrl"] = BuildQrUrl(orderNo);
        result.jsondata["expireAt"] = rows.empty() ? "" : rows[0]["expire_at"].as<std::string>();
        result.setResult(ApiErrorCode::ApiError_Success, "success");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "CreateOrder 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    co_return result;
}

Task<HttpResult> BillService::GetOrder(int userId, const std::string &orderNo)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        auto load = [&]() -> std::optional<drogon::orm::Row> {
            auto rows = db->execSqlSync(
                "select order_no, amount_cents, channel::int as channel, status::int as status, "
                "       qr_content, pay_url, "
                "       (expire_at is not null and expire_at < now()) as expired, "
                "       to_char(created_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD HH24:MI:SS') as created_at, "
                "       to_char(paid_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD HH24:MI:SS') as paid_at "
                "from payment_order where order_no = $1 and user_id = $2",
                orderNo, userId);
            if (rows.empty())
            {
                return std::optional<drogon::orm::Row>{};
            }
            return std::optional<drogon::orm::Row>{rows[0]};
        };

        auto row = load();
        if (!row.has_value())
        {
            result.setResult(ApiErrorCode::ApiError_OrderNotFound);
            co_return result;
        }

        int status = (*row)["status"].as<int>();
        const int channel = (*row)["channel"].as<int>();
        const bool expired = (*row)["expired"].as<bool>();

        // 回调可能丢失：轮询查单时主动向渠道确认一次（同一订单 4 秒内最多一次）
        if (status == 0 && !expired && (channel == 1 || channel == 2) &&
            PaymentConfig::ChannelReady(channel) && ShouldQueryProvider(orderNo))
        {
            PayQueryResult query = co_await QueryProvider(channel, orderNo);
            if (query.ok)
            {
                if (query.paid)
                {
                    std::string settleError;
                    if (!SettleOrder(orderNo, channel, query.providerTradeNo, query.paidCents, "query", settleError))
                    {
                        LOG_WARN << "[Payment][query] 查单已支付但入账失败: orderNo=" << orderNo
                                 << " reason=" << settleError;
                    }
                }
                else if (query.closed)
                {
                    db->execSqlSync("update payment_order set status = 2 where order_no = $1 and status = 0", orderNo);
                }
            }
            else if (!query.error.empty())
            {
                LOG_DEBUG << "[Payment][query] 查单失败 orderNo=" << orderNo << " reason=" << query.error;
            }
            row = load();
            if (!row.has_value())
            {
                result.setResult(ApiErrorCode::ApiError_OrderNotFound);
                co_return result;
            }
            status = (*row)["status"].as<int>();
        }

        result.jsondata["orderNo"] = (*row)["order_no"].as<std::string>();
        result.jsondata["amountCents"] = static_cast<Json::Int64>((*row)["amount_cents"].as<int64_t>());
        result.jsondata["channel"] = channel;
        result.jsondata["status"] = status;
        result.jsondata["paid"] = (status == 1);
        result.jsondata["qrContent"] = (*row)["qr_content"].isNull() ? "" : (*row)["qr_content"].as<std::string>();
        result.jsondata["payUrl"] = (*row)["pay_url"].isNull() ? "" : (*row)["pay_url"].as<std::string>();
        result.jsondata["qrPngUrl"] = BuildQrUrl(orderNo);
        result.jsondata["createdAt"] = (*row)["created_at"].as<std::string>();
        result.jsondata["paidAt"] = (*row)["paid_at"].isNull() ? "" : (*row)["paid_at"].as<std::string>();

        if (status == 1)
        {
            auto bal = db->execSqlSync("select balance_cents from user_balance where user_id = $1", userId);
            result.jsondata["balanceAfterCents"] =
                bal.empty() ? 0 : static_cast<Json::Int64>(bal[0]["balance_cents"].as<int64_t>());

            auto refunds = db->execSqlSync(
                "select id, refund_no, status::int as status, amount_cents, reason, review_remark, error_msg, "
                "       to_char(created_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD HH24:MI:SS') as created_at, "
                "       to_char(reviewed_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD HH24:MI:SS') as reviewed_at, "
                "       to_char(refunded_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD HH24:MI:SS') as refunded_at "
                "from payment_refund where order_no = $1 order by id desc limit 1",
                orderNo);
            if (!refunds.empty())
            {
                const auto &rf = refunds[0];
                Json::Value refund;
                refund["id"] = static_cast<Json::Int64>(rf["id"].as<long long>());
                refund["refundNo"] = rf["refund_no"].as<std::string>();
                const int rs = rf["status"].as<int>();
                refund["status"] = rs;
                refund["statusText"] = RefundService::StatusText(rs);
                refund["amountCents"] = static_cast<Json::Int64>(rf["amount_cents"].as<int64_t>());
                refund["reason"] = rf["reason"].isNull() ? "" : rf["reason"].as<std::string>();
                refund["reviewRemark"] = rf["review_remark"].isNull() ? "" : rf["review_remark"].as<std::string>();
                refund["errorMsg"] = rf["error_msg"].isNull() ? "" : rf["error_msg"].as<std::string>();
                refund["createdAt"] = rf["created_at"].as<std::string>();
                refund["reviewedAt"] = rf["reviewed_at"].isNull() ? "" : rf["reviewed_at"].as<std::string>();
                refund["refundedAt"] = rf["refunded_at"].isNull() ? "" : rf["refunded_at"].as<std::string>();
                result.jsondata["refund"] = refund;
            }
        }

        result.setResult(ApiErrorCode::ApiError_Success, "success");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "GetOrder 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    co_return result;
}

HttpResult BillService::ListOrders(int userId, int page, int size)
{
    HttpResult result;
    if (page <= 0)
    {
        page = 1;
    }
    if (size <= 0 || size > 100)
    {
        size = 20;
    }

    auto db = drogon::app().getDbClient();
    try
    {
        auto countRows = db->execSqlSync(
            "select count(*) as total from payment_order where user_id = $1 and status = 1", userId);
        const int64_t total = countRows.empty() ? 0 : countRows[0]["total"].as<int64_t>();

        auto rows = db->execSqlSync(
            "select o.order_no, o.amount_cents, o.channel::int as channel, o.status::int as status, "
            "       to_char(o.paid_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD HH24:MI:SS') as paid_at, "
            "       r.id as refund_id, r.refund_no, r.status::int as refund_status, "
            "       r.amount_cents as refund_amount_cents, r.reason as refund_reason, "
            "       r.review_remark as refund_review_remark, r.error_msg as refund_error, "
            "       to_char(r.created_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD HH24:MI:SS') as refund_created_at, "
            "       to_char(r.refunded_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD HH24:MI:SS') as refund_refunded_at "
            "from payment_order o "
            "left join lateral ("
            "    select * from payment_refund where order_no = o.order_no order by id desc limit 1"
            ") r on true "
            "where o.user_id = $1 and o.status = 1 "
            "order by o.paid_at desc nulls last, o.id desc limit $2::int offset $3::int",
            userId, size, (page - 1) * size);

        Json::Value items(Json::arrayValue);
        for (const auto &r : rows)
        {
            Json::Value item;
            item["orderNo"] = r["order_no"].as<std::string>();
            item["amountCents"] = static_cast<Json::Int64>(r["amount_cents"].as<int64_t>());
            item["channel"] = r["channel"].as<int>();
            item["status"] = r["status"].as<int>();
            item["paidAt"] = r["paid_at"].isNull() ? "" : r["paid_at"].as<std::string>();
            if (!r["refund_id"].isNull())
            {
                Json::Value refund;
                refund["id"] = static_cast<Json::Int64>(r["refund_id"].as<long long>());
                refund["refundNo"] = r["refund_no"].as<std::string>();
                const int rs = r["refund_status"].as<int>();
                refund["status"] = rs;
                refund["statusText"] = RefundService::StatusText(rs);
                refund["amountCents"] = static_cast<Json::Int64>(r["refund_amount_cents"].as<int64_t>());
                refund["reason"] = r["refund_reason"].isNull() ? "" : r["refund_reason"].as<std::string>();
                refund["reviewRemark"] =
                    r["refund_review_remark"].isNull() ? "" : r["refund_review_remark"].as<std::string>();
                refund["errorMsg"] = r["refund_error"].isNull() ? "" : r["refund_error"].as<std::string>();
                refund["createdAt"] = r["refund_created_at"].as<std::string>();
                refund["refundedAt"] =
                    r["refund_refunded_at"].isNull() ? "" : r["refund_refunded_at"].as<std::string>();
                item["refund"] = refund;
            }
            items.append(item);
        }

        result.jsondata["total"] = static_cast<Json::Int64>(total);
        result.jsondata["page"] = page;
        result.jsondata["size"] = size;
        result.jsondata["items"] = items;
        result.setResult(ApiErrorCode::ApiError_Success, "success");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "[Bill] ListOrders 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult BillService::ListTransactions(int userId, int type, int page, int size)
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
            "select count(*) as total from balance_txn where user_id = $1 and ($2::int = 0 or type::int = $2::int)",
            userId, type);
        const int64_t total = countRows.empty() ? 0 : countRows[0]["total"].as<int64_t>();

        auto rows = db->execSqlSync(
            "select id, type::int as type, amount_cents, balance_after, remark, "
            "       to_char(created_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD HH24:MI:SS') as created_at "
            "from balance_txn where user_id = $1 and ($2::int = 0 or type::int = $2::int) "
            "order by id desc limit $3::int offset $4::int",
            userId, type, size, (page - 1) * size);

        Json::Value items(Json::arrayValue);
        for (const auto &r : rows)
        {
            Json::Value item;
            item["id"] = static_cast<Json::Int64>(r["id"].as<int64_t>());
            item["type"] = r["type"].as<int>();
            item["amountCents"] = static_cast<Json::Int64>(r["amount_cents"].as<int64_t>());
            item["balanceAfterCents"] = static_cast<Json::Int64>(r["balance_after"].as<int64_t>());
            item["remark"] = r["remark"].isNull() ? "" : r["remark"].as<std::string>();
            item["createdAt"] = r["created_at"].as<std::string>();
            items.append(item);
        }

        result.jsondata["total"] = static_cast<Json::Int64>(total);
        result.jsondata["page"] = page;
        result.jsondata["size"] = size;
        result.jsondata["items"] = items;
        result.setResult(ApiErrorCode::ApiError_Success, "success");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "ListTransactions 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult BillService::MockPaid(int userId, const std::string &orderNo)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        auto orderRows = db->execSqlSync(
            "select status::int as status, channel::int as channel, amount_cents from payment_order where order_no = $1 and user_id = $2",
            orderNo, userId);
        if (orderRows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_OrderNotFound);
            return result;
        }

        const int status = orderRows[0]["status"].as<int>();
        const int channel = orderRows[0]["channel"].as<int>();
        if (status == 2)
        {
            result.setResult(ApiErrorCode::ApiError_OrderStateInvalid);
            return result;
        }
        if (status == 1)
        {
            // 幂等：已支付直接返回当前余额，不重复入账
            auto bal = db->execSqlSync("select balance_cents from user_balance where user_id = $1", userId);
            result.jsondata["orderNo"] = orderNo;
            result.jsondata["paid"] = true;
            result.jsondata["balanceAfterCents"] =
                bal.empty() ? 0 : static_cast<Json::Int64>(bal[0]["balance_cents"].as<int64_t>());
            result.setResult(ApiErrorCode::ApiError_Success, "already paid");
            return result;
        }

        if (channel != 9 && !IsTestMode())
        {
            result.setResult(ApiErrorCode::ApiError_MockPayDisabled);
            return result;
        }

        std::string settleError;
        if (!SettleOrder(orderNo, channel, "", orderRows[0]["amount_cents"].as<int64_t>(), "mock", settleError))
        {
            result.setResult(ApiErrorCode::ApiError_DatabaseError, settleError);
            return result;
        }

        auto bal = db->execSqlSync(
            "select balance_cents from user_balance where user_id = $1", userId);
        result.jsondata["orderNo"] = orderNo;
        result.jsondata["paid"] = true;
        result.jsondata["amountCents"] = static_cast<Json::Int64>(orderRows[0]["amount_cents"].as<int64_t>());
        result.jsondata["balanceAfterCents"] =
            bal.empty() ? 0 : static_cast<Json::Int64>(bal[0]["balance_cents"].as<int64_t>());
        result.setResult(ApiErrorCode::ApiError_Success, "paid");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "MockPaid 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

NotifyReply BillService::HandleAlipayNotify(const std::string &body,
                                           const std::map<std::string, std::string> &headers)
{
    NotifyReply reply;
    PayNotifyResult notify = AlipayProvider::ParseNotify(body);
    LogCallback(notify.orderNo, 1, body, headers, notify.ok);

    if (!notify.ok)
    {
        reply.message = notify.error;
        LOG_WARN << "[Payment][alipay-notify] " << notify.error;
        return reply;
    }
    if (!notify.paid)
    {
        // 非成功状态（如 WAIT_BUYER_PAY / TRADE_CLOSED）只确认收到，不入账
        reply.ok = true;
        reply.message = "success";
        return reply;
    }

    std::string error;
    if (!SettleOrder(notify.orderNo, 1, notify.providerTradeNo, notify.paidCents, "alipay-notify", error))
    {
        reply.message = error;
        return reply;
    }
    reply.ok = true;
    reply.message = "success";
    return reply;
}

NotifyReply BillService::HandleWechatNotify(const std::string &body,
                                           const std::map<std::string, std::string> &headers)
{
    NotifyReply reply;
    PayNotifyResult notify = WechatPayProvider::ParseNotify(headers, body);
    LogCallback(notify.orderNo, 2, body, headers, notify.ok);

    if (!notify.ok)
    {
        reply.message = notify.error;
        LOG_WARN << "[Payment][wechat-notify] " << notify.error;
        return reply;
    }
    if (!notify.paid)
    {
        reply.ok = true;
        reply.message = "成功";
        return reply;
    }

    std::string error;
    if (!SettleOrder(notify.orderNo, 2, notify.providerTradeNo, notify.paidCents, "wechat-notify", error))
    {
        reply.message = error;
        return reply;
    }
    reply.ok = true;
    reply.message = "成功";
    return reply;
}

bool BillService::GetQrPng(const std::string &orderNo, std::string &png)
{
    png.clear();
    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync(
            "select qr_content, channel::int as channel from payment_order where order_no = $1",
            orderNo);
        if (rows.empty())
        {
            return false;
        }
        const int channel = rows[0]["channel"].as<int>();
        const std::string qrContent = rows[0]["qr_content"].isNull() ? "" : rows[0]["qr_content"].as<std::string>();
        if (channel == 9 || qrContent.empty())
        {
            return false;
        }
        png = RenderQrPng(qrContent);
        return !png.empty();
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "[Payment] 读取二维码失败: " << e.base().what();
        return false;
    }
}

} // namespace Services
} // namespace UEAdminAPI

#include "BillService.h"

#include <drogon/drogon.h>

#include "utils/ApiErrorCodes.h"

#include <ctime>
#include <random>
#include <sstream>

using namespace drogon;
using namespace UEAdminAPI;
using namespace UEAdminAPI::utils;

namespace UEAdminAPI {
namespace Services {

namespace {

constexpr int64_t kMaxAmountCents = 10000000;  // 单笔上限 10 万元
constexpr int kOrderExpireMinutes = 30;

bool IsMockPayEnabled()
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

} // namespace

HttpResult BillService::CreateOrder(int userId, int64_t amountCents, int channel, const std::string &idemKey)
{
    HttpResult result;
    if (amountCents <= 0 || amountCents > kMaxAmountCents)
    {
        result.setResult(ApiErrorCode::ApiError_AmountInvalid);
        return result;
    }
    if (channel != 1 && channel != 2 && channel != 9)
    {
        result.setResult(ApiErrorCode::ApiError_PayChannelUnsupported);
        return result;
    }

    auto db = drogon::app().getDbClient();
    try
    {
        // 幂等：同一 idemKey 直接返回原订单
        if (!idemKey.empty())
        {
            auto exist = db->execSqlSync(
                "select order_no, amount_cents, channel::int as channel, status::int as status, "
                "       to_char(expire_at, 'YYYY-MM-DD HH24:MI:SS') as expire_at "
                "from payment_order where user_id = $1 and idem_key = $2",
                userId, idemKey);
            if (!exist.empty())
            {
                const auto &r = exist[0];
                result.jsondata["orderNo"] = r["order_no"].as<std::string>();
                result.jsondata["amountCents"] = static_cast<Json::Int64>(r["amount_cents"].as<int64_t>());
                result.jsondata["channel"] = r["channel"].as<int>();
                result.jsondata["status"] = r["status"].as<int>();
                result.jsondata["expireAt"] = r["expire_at"].isNull() ? "" : r["expire_at"].as<std::string>();
                result.jsondata["qrContent"] = "mockpay://order/" + r["order_no"].as<std::string>();
                result.jsondata["qrPngUrl"] = BuildQrUrl(r["order_no"].as<std::string>());
                result.setResult(ApiErrorCode::ApiError_Success, "order exists");
                return result;
            }
        }

        const std::string orderNo = GenerateOrderNo();
        const std::string qrContent = "mockpay://order/" + orderNo;
        const std::string idemValue = idemKey.empty() ? orderNo : idemKey;

        auto rows = db->execSqlSync(
            "insert into payment_order (order_no, user_id, amount_cents, channel, status, qr_content, idem_key, expire_at) "
            "values ($1, $2, $3, $4::int, 0, $5, $6, now() + ($7::int * interval '1 minute')) "
            "returning to_char(expire_at, 'YYYY-MM-DD HH24:MI:SS') as expire_at",
            orderNo, userId, amountCents, channel, qrContent, idemValue, std::to_string(kOrderExpireMinutes));

        result.jsondata["orderNo"] = orderNo;
        result.jsondata["amountCents"] = static_cast<Json::Int64>(amountCents);
        result.jsondata["channel"] = channel;
        result.jsondata["status"] = 0;
        result.jsondata["qrContent"] = qrContent;
        result.jsondata["qrPngUrl"] = BuildQrUrl(orderNo);
        result.jsondata["expireAt"] = rows.empty() ? "" : rows[0]["expire_at"].as<std::string>();
        result.setResult(ApiErrorCode::ApiError_Success, "success");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "CreateOrder 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult BillService::GetOrder(int userId, const std::string &orderNo)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync(
            "select order_no, amount_cents, channel::int as channel, status::int as status, "
            "       to_char(created_at, 'YYYY-MM-DD HH24:MI:SS') as created_at, "
            "       to_char(paid_at, 'YYYY-MM-DD HH24:MI:SS') as paid_at "
            "from payment_order where order_no = $1 and user_id = $2",
            orderNo, userId);
        if (rows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_OrderNotFound);
            return result;
        }

        const auto &r = rows[0];
        const int status = r["status"].as<int>();
        result.jsondata["orderNo"] = r["order_no"].as<std::string>();
        result.jsondata["amountCents"] = static_cast<Json::Int64>(r["amount_cents"].as<int64_t>());
        result.jsondata["channel"] = r["channel"].as<int>();
        result.jsondata["status"] = status;
        result.jsondata["paid"] = (status == 1);
        result.jsondata["createdAt"] = r["created_at"].as<std::string>();
        result.jsondata["paidAt"] = r["paid_at"].isNull() ? "" : r["paid_at"].as<std::string>();

        if (status == 1)
        {
            auto bal = db->execSqlSync(
                "select balance_cents from user_balance where user_id = $1", userId);
            result.jsondata["balanceAfterCents"] =
                bal.empty() ? 0 : static_cast<Json::Int64>(bal[0]["balance_cents"].as<int64_t>());
        }

        result.setResult(ApiErrorCode::ApiError_Success, "success");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "GetOrder 失败: " << e.base().what();
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
            "       to_char(created_at, 'YYYY-MM-DD HH24:MI:SS') as created_at "
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

        if (channel != 9 && !IsMockPayEnabled())
        {
            result.setResult(ApiErrorCode::ApiError_MockPayDisabled);
            return result;
        }

        // 单条 CTE 完成「置为已支付 + 建余额行 + 加余额 + 记流水」，天然原子且并发安全：
        // update 会锁住订单行，并发的第二个调用拿不到 status=0 的行，因此不会重复入账。
        const std::string extRef = "order:" + orderNo;
        auto rows = db->execSqlSync(
            "with o as ("
            "    update payment_order set status = 1, paid_at = now() "
            "    where order_no = $1 and user_id = $2 and status = 0 "
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
            ") select up.balance_cents, up.amount_cents from up",
            orderNo, userId, extRef);

        if (rows.empty())
        {
            // 并发竞争：另一个请求已完成入账
            auto bal = db->execSqlSync("select balance_cents from user_balance where user_id = $1", userId);
            result.jsondata["orderNo"] = orderNo;
            result.jsondata["paid"] = true;
            result.jsondata["balanceAfterCents"] =
                bal.empty() ? 0 : static_cast<Json::Int64>(bal[0]["balance_cents"].as<int64_t>());
            result.setResult(ApiErrorCode::ApiError_Success, "already paid");
            return result;
        }

        result.jsondata["orderNo"] = orderNo;
        result.jsondata["paid"] = true;
        result.jsondata["amountCents"] = static_cast<Json::Int64>(rows[0]["amount_cents"].as<int64_t>());
        result.jsondata["balanceAfterCents"] = static_cast<Json::Int64>(rows[0]["balance_cents"].as<int64_t>());
        result.setResult(ApiErrorCode::ApiError_Success, "paid");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "MockPaid 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

} // namespace Services
} // namespace UEAdminAPI

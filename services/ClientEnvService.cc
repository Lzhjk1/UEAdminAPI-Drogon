#include "ClientEnvService.h"

#include <drogon/drogon.h>

#include "utils/ApiErrorCodes.h"

#include <algorithm>
#include <cctype>
#include <string>

using namespace drogon;
using namespace UEAdminAPI;
using namespace UEAdminAPI::utils;

namespace UEAdminAPI {
namespace Services {

namespace {

constexpr size_t kMaxPayloadBytes = 256 * 1024;
constexpr size_t kMaxDeviceKeyLen = 64;
constexpr size_t kMinDeviceKeyLen = 16;
constexpr size_t kMaxVersionBytes = 64;
constexpr size_t kMaxKeywordBytes = 100;

std::string Trim(const std::string &text)
{
    const auto begin = std::find_if_not(text.begin(), text.end(),
                                        [](unsigned char ch) { return std::isspace(ch) != 0; });
    const auto end = std::find_if_not(text.rbegin(), text.rend(),
                                      [](unsigned char ch) { return std::isspace(ch) != 0; }).base();
    if (begin >= end)
    {
        return std::string();
    }
    return std::string(begin, end);
}

bool IsHexString(const std::string &text)
{
    return std::all_of(text.begin(), text.end(), [](unsigned char ch) {
        return std::isxdigit(ch) != 0;
    });
}

std::string JsonString(const Json::Value &json, const char *key)
{
    if (json.isMember(key) && json[key].isString())
    {
        return json[key].asString();
    }
    return std::string();
}

HttpResult ErrorResult(ApiErrorCode code, const std::string &msg)
{
    HttpResult result;
    result.setResult(code, msg);
    return result;
}

} // namespace

HttpResult ClientEnvService::Submit(int userId, const Json::Value &body)
{
    if (!body.isObject())
    {
        return ErrorResult(ApiError_InvalidJsonFormat, "请求体必须是 JSON 对象");
    }

    const std::string deviceKey = Trim(JsonString(body, "deviceKey"));
    if (deviceKey.size() < kMinDeviceKeyLen || deviceKey.size() > kMaxDeviceKeyLen ||
        !IsHexString(deviceKey))
    {
        return ErrorResult(ApiError_MissingRequiredArgs, "deviceKey 必须是 16-64 位十六进制字符串");
    }

    std::string productVersion = Trim(JsonString(body, "productVersion"));
    if (productVersion.size() > kMaxVersionBytes)
    {
        productVersion = productVersion.substr(0, kMaxVersionBytes);
    }

    Json::FastWriter writer;
    const std::string payloadText = writer.write(body);
    if (payloadText.size() > kMaxPayloadBytes)
    {
        return ErrorResult(ApiError_InvalidOperation, "环境信息过大");
    }

    auto db = drogon::app().getDbClient();
    std::string userName;
    try
    {
        auto rows = db->execSqlSync("select name from \"user\" where id = $1", userId);
        if (rows.empty())
        {
            return ErrorResult(ApiError_UserNotFound, "用户不存在");
        }
        userName = rows[0]["name"].as<std::string>();
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "ClientEnvService::Submit 查询用户失败: " << e.base().what();
        return ErrorResult(ApiError_DatabaseError, "数据库错误");
    }

    try
    {
        db->execSqlSync(
            "insert into client_env_report "
            "(user_id, user_name, device_key, product_version, payload, first_seen, last_seen) "
            "values ($1, $2, $3, $4, $5::jsonb, now(), now()) "
            "on conflict (user_id, device_key) do update set "
            "  user_name = excluded.user_name, "
            "  product_version = excluded.product_version, "
            "  payload = excluded.payload, "
            "  last_seen = now()",
            userId, userName, deviceKey, productVersion, payloadText);
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "ClientEnvService::Submit 写入失败: " << e.base().what();
        return ErrorResult(ApiError_DatabaseError, "数据库错误");
    }

    HttpResult result;
    result.jsondata["ok"] = true;
    result.setResult(ApiError_Success);
    return result;
}

HttpResult ClientEnvService::AdminList(const std::string &keyword, int page, int size)
{
    if (page < 1)
    {
        page = 1;
    }
    if (size < 1 || size > 200)
    {
        size = 50;
    }
    const int offset = (page - 1) * size;

    std::string kw = Trim(keyword);
    if (kw.size() > kMaxKeywordBytes)
    {
        kw = kw.substr(0, kMaxKeywordBytes);
    }

    auto db = drogon::app().getDbClient();
    try
    {
        auto totalRows = db->execSqlSync(
            "select count(*) as total from client_env_report r "
            "where ($1 = '' or r.user_name ilike '%' || $1 || '%' "
            "       or r.product_version ilike '%' || $1 || '%')",
            kw);
        const long long total = totalRows.empty() ? 0 : totalRows[0]["total"].as<long long>();

        auto rows = db->execSqlSync(
            "select r.id, coalesce(r.user_id, 0) as user_id, r.user_name, r.device_key, "
            "       r.product_version, "
            "       coalesce(r.payload #>> '{cad,product}', '') as cad_product, "
            "       coalesce(r.payload #>> '{cad,version}', '') as cad_version, "
            "       coalesce(r.payload #>> '{os,name}', '') as os_name, "
            "       coalesce(r.payload #>> '{cpu,name}', '') as cpu_name, "
            "       coalesce((r.payload #>> '{memory,totalMb}')::bigint, 0) as memory_mb, "
            "       to_char(r.first_seen at time zone 'Asia/Shanghai', 'YYYY-MM-DD HH24:MI:SS') as first_seen, "
            "       to_char(r.last_seen at time zone 'Asia/Shanghai', 'YYYY-MM-DD HH24:MI:SS') as last_seen, "
            "       r.payload::text as payload "
            "from client_env_report r "
            "where ($1 = '' or r.user_name ilike '%' || $1 || '%' "
            "       or r.product_version ilike '%' || $1 || '%') "
            "order by r.last_seen desc limit $2::int offset $3::int",
            kw, size, offset);

        Json::Value items(Json::arrayValue);
        for (const auto &row : rows)
        {
            Json::Value item;
            item["id"] = row["id"].as<int>();
            item["userId"] = row["user_id"].as<int>();
            item["userName"] = row["user_name"].as<std::string>();
            item["deviceKey"] = row["device_key"].as<std::string>();
            item["productVersion"] = row["product_version"].as<std::string>();
            item["cadProduct"] = row["cad_product"].as<std::string>();
            item["cadVersion"] = row["cad_version"].as<std::string>();
            item["osName"] = row["os_name"].as<std::string>();
            item["cpuName"] = row["cpu_name"].as<std::string>();
            item["memoryMb"] = static_cast<Json::Int64>(row["memory_mb"].as<long long>());
            item["firstSeen"] = row["first_seen"].as<std::string>();
            item["lastSeen"] = row["last_seen"].as<std::string>();
            item["payload"] = row["payload"].as<std::string>();
            items.append(item);
        }

        HttpResult result;
        result.jsondata["total"] = static_cast<Json::Int64>(total);
        result.jsondata["page"] = page;
        result.jsondata["size"] = size;
        result.jsondata["items"] = items;
        result.setResult(ApiError_Success);
        return result;
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "ClientEnvService::AdminList 查询失败: " << e.base().what();
        return ErrorResult(ApiError_DatabaseError, "数据库错误");
    }
}

} // namespace Services
} // namespace UEAdminAPI

#include "AiController.h"

#include "services/TeamService.h"
#include "utils/ApiErrorCodes.h"
#include "utils/HttpResult.h"

#include <drogon/HttpClient.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <trantor/utils/Logger.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

using namespace drogon;
using namespace UEAdminAPI;
using namespace UEAdminAPI::Services;
using namespace UEAdminAPI::utils;

namespace
{

int CurrentUserId(const HttpRequestPtr &req)
{
    int userId = -1;
    auto attributes = req->getAttributes();
    if (attributes->find("userId"))
    {
        userId = attributes->get<int>("userId");
    }
    return userId;
}

HttpResponsePtr JsonResponse(const HttpResult &result)
{
    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    return resp;
}

struct AiConfig
{
    bool enabled = false;
    std::string baseUrl;
    std::string apiKey;
    std::string model;
    double timeoutSec = 180.0;
    int maxTokens = 0;
};

std::string EnvString(const char *name)
{
    const char *value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

AiConfig LoadAiConfig()
{
    AiConfig config;
    const auto custom = drogon::app().getCustomConfig();
    if (custom.isMember("AI") && custom["AI"].isObject())
    {
        const auto &ai = custom["AI"];
        config.enabled = ai.get("Enabled", true).asBool();
        config.baseUrl = ai.get("BaseUrl", "").asString();
        config.apiKey = ai.get("ApiKey", "").asString();
        config.model = ai.get("Model", "").asString();
        config.timeoutSec = ai.get("TimeoutSec", 180).asDouble();
        config.maxTokens = ai.get("MaxTokens", 0).asInt();
    }

    // 部署侧可用环境变量覆盖，密钥不必写进配置文件；测试也用同一入口。
    const std::string enabledEnv = EnvString("UE_AI_ENABLED");
    if (!enabledEnv.empty())
    {
        config.enabled = (enabledEnv == "1" || enabledEnv == "true" ||
                          enabledEnv == "yes" || enabledEnv == "on");
    }
    const std::string baseEnv = EnvString("UE_AI_BASE_URL");
    if (!baseEnv.empty()) config.baseUrl = baseEnv;
    const std::string keyEnv = EnvString("UE_AI_KEY");
    if (!keyEnv.empty()) config.apiKey = keyEnv;
    const std::string modelEnv = EnvString("UE_AI_MODEL");
    if (!modelEnv.empty()) config.model = modelEnv;
    const std::string timeoutEnv = EnvString("UE_AI_TIMEOUT_SEC");
    if (!timeoutEnv.empty()) config.timeoutSec = std::atof(timeoutEnv.c_str());
    const std::string maxTokensEnv = EnvString("UE_AI_MAX_TOKENS");
    if (!maxTokensEnv.empty()) config.maxTokens = std::atoi(maxTokensEnv.c_str());

    if (config.timeoutSec < 10.0)
    {
        config.timeoutSec = 10.0;
    }
    return config;
}

std::shared_ptr<HttpClient> ClientFor(const std::string &baseUrl, std::string &pathPrefix)
{
    static std::mutex mutex;
    static std::string cachedHost;
    static std::shared_ptr<HttpClient> cachedClient;

    const auto schemeEnd = baseUrl.find("://");
    const size_t hostStart = schemeEnd == std::string::npos ? 0 : schemeEnd + 3;
    const auto pathStart = baseUrl.find('/', hostStart);
    const std::string host =
        baseUrl.substr(0, pathStart == std::string::npos ? baseUrl.size() : pathStart);
    std::string prefix = pathStart == std::string::npos ? "" : baseUrl.substr(pathStart);
    while (!prefix.empty() && prefix.back() == '/')
    {
        prefix.pop_back();
    }
    pathPrefix = prefix + "/chat/completions";

    std::lock_guard<std::mutex> lock(mutex);
    if (!cachedClient || cachedHost != host)
    {
        cachedClient = HttpClient::newHttpClient(host);
        cachedHost = host;
    }
    return cachedClient;
}

bool ParseJson(std::string_view text, Json::Value &out)
{
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    std::string errors;
    return reader->parse(text.data(), text.data() + text.size(), &out, &errors);
}

std::string ProviderMessage(const Json::Value &payload)
{
    if (payload.isMember("error") && payload["error"].isObject())
    {
        const auto &error = payload["error"];
        if (error.isMember("message") && error["message"].isString())
        {
            return error["message"].asString();
        }
    }
    if (payload.isMember("message") && payload["message"].isString())
    {
        return payload["message"].asString();
    }
    return "";
}

// ---------------------------------------------------------------- 用量记账

struct TokenUsage
{
    std::string model;
    int promptTokens = 0;
    int completionTokens = 0;
    int totalTokens = 0;
    int cacheHitTokens = 0;
    int cacheMissTokens = 0;
};

bool ExtractUsage(const Json::Value &payload, TokenUsage &usage)
{
    if (!payload.isMember("usage") || !payload["usage"].isObject())
    {
        return false;
    }
    const auto &u = payload["usage"];
    usage.promptTokens = u.get("prompt_tokens", 0).asInt();
    usage.completionTokens = u.get("completion_tokens", 0).asInt();
    usage.totalTokens = u.get("total_tokens", usage.promptTokens + usage.completionTokens).asInt();
    // DeepSeek 直接给出命中/未命中；其它 OpenAI 兼容厂商只给 cached_tokens。
    usage.cacheHitTokens = u.get("prompt_cache_hit_tokens", 0).asInt();
    usage.cacheMissTokens = u.get("prompt_cache_miss_tokens", 0).asInt();
    if (usage.cacheHitTokens == 0 && u.isMember("prompt_tokens_details") &&
        u["prompt_tokens_details"].isObject())
    {
        usage.cacheHitTokens = u["prompt_tokens_details"].get("cached_tokens", 0).asInt();
    }
    if (usage.cacheMissTokens == 0 && usage.promptTokens > usage.cacheHitTokens)
    {
        usage.cacheMissTokens = usage.promptTokens - usage.cacheHitTokens;
    }
    if (payload.isMember("model") && payload["model"].isString())
    {
        usage.model = payload["model"].asString();
    }
    return true;
}

void RecordUsage(int userId, const TokenUsage &usage)
{
    auto db = drogon::app().getDbClient();
    if (!db)
    {
        return;
    }
    try
    {
        // 记账失败不影响模型响应；团队取记账时用户所在团队。
        db->execSqlAsync(
            "insert into ai_usage_log "
            "(user_id, team_id, model, prompt_tokens, completion_tokens, total_tokens, "
            " cache_hit_tokens, cache_miss_tokens) "
            "values ($1, (select tm.team_id from team_member tm "
            "             where tm.user_id = $2 and tm.status = 1 "
            "             order by tm.team_id limit 1), "
            "        $3, $4, $5, $6, $7, $8)",
            [](const drogon::orm::Result &) {},
            [](const drogon::orm::DrogonDbException &e) {
                LOG_ERROR << "AI 用量记账失败: " << e.base().what();
            },
            userId,
            userId,
            usage.model,
            usage.promptTokens,
            usage.completionTokens,
            usage.totalTokens,
            usage.cacheHitTokens,
            usage.cacheMissTokens);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "AI 用量记账异常: " << e.what();
    }
}

int TokenQuotaForPlan(const std::string &plan, bool trial)
{
    Json::Value quota;
    const auto custom = drogon::app().getCustomConfig();
    if (custom.isMember("Subscription") && custom["Subscription"].isObject() &&
        custom["Subscription"].isMember("TokenQuota") &&
        custom["Subscription"]["TokenQuota"].isObject())
    {
        quota = custom["Subscription"]["TokenQuota"];
    }

    std::string key = trial ? "trial" : plan;
    if (key == "standard" || key == "legacy")
    {
        key = "pro";
    }
    if (key.empty())
    {
        key = "free";
    }
    if (quota.isMember(key) && quota[key].isInt())
    {
        return std::max(0, quota[key].asInt());
    }
    if (key == "free")
    {
        return 10000000;
    }
    if (key == "trial")
    {
        return 200000000;
    }
    if (key == "plus")
    {
        return 400000000;
    }
    if (key == "pro")
    {
        return 1000000000;
    }
    if (key == "max")
    {
        return 2000000000;
    }
    return 0;
}

long long MonthlyTeamTokens(int userId)
{
    auto db = drogon::app().getDbClient();
    if (!db)
    {
        return 0;
    }
    auto rows = db->execSqlSync(
        "select coalesce(sum(u.total_tokens), 0) as used "
        "from ai_usage_log u "
        "where u.team_id = (select tm.team_id from team_member tm "
        "                    where tm.user_id = $1 and tm.status = 1 limit 1) "
        "  and u.created_at >= (date_trunc('month', now() at time zone 'Asia/Shanghai') "
        "                       at time zone 'Asia/Shanghai') "
        "  and u.created_at < ((date_trunc('month', now() at time zone 'Asia/Shanghai') "
        "                       + interval '1 month') at time zone 'Asia/Shanghai')",
        userId);
    return rows.empty() ? 0 : rows[0]["used"].as<long long>();
}

int TeamIdForUser(int userId)
{
    try
    {
        auto db = drogon::app().getDbClient();
        if (!db)
        {
            return 0;
        }
        auto rows = db->execSqlSync(
            "select tm.team_id from team_member tm "
            "join team t on t.id = tm.team_id "
            "where tm.user_id = $1 and tm.status = 1 and t.status = 1 "
            "order by tm.team_id limit 1",
            userId);
        return rows.empty() ? 0 : rows[0]["team_id"].as<int>();
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_WARN << "读取用户团队失败: " << e.base().what();
        return 0;
    }
}

long long AddonCreditTokens(int teamId)
{
    if (teamId <= 0)
    {
        return 0;
    }
    try
    {
        auto db = drogon::app().getDbClient();
        if (!db)
        {
            return 0;
        }
        auto rows = db->execSqlSync(
            "select coalesce(sum(amount), 0) as credits from team_addon "
            "where team_id = $1 and kind = 'credits' and status = 1 and end_at > now()",
            teamId);
        return rows.empty() ? 0 : rows[0]["credits"].as<long long>() * 100000;
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_WARN << "读取加购积分失败: " << e.base().what();
        return 0;
    }
}

Json::Value QuotaJson(const std::string &plan, bool trial, long long monthly, long long used)
{
    Json::Value quota;
    quota["plan"] = plan;
    quota["trial"] = trial;
    quota["monthlyTokens"] = static_cast<Json::Int64>(monthly);
    quota["usedTokens"] = static_cast<Json::Int64>(used);
    quota["remainingTokens"] = static_cast<Json::Int64>(std::max<long long>(0, monthly - used));
    quota["monthlyCredits"] = static_cast<Json::Int64>(monthly / 100000);
    quota["usedCredits"] = static_cast<Json::Int64>(used / 100000);
    quota["remainingCredits"] =
        static_cast<Json::Int64>(std::max<long long>(0, monthly - used) / 100000);
    return quota;
}

bool ValidMonth(const std::string &month)
{
    if (month.size() != 7 || month[4] != '-')
    {
        return false;
    }
    for (const int i : {0, 1, 2, 3, 5, 6})
    {
        if (!std::isdigit(static_cast<unsigned char>(month[i])))
        {
            return false;
        }
    }
    const int m = (month[5] - '0') * 10 + (month[6] - '0');
    return m >= 1 && m <= 12;
}

Json::Value UsageSummaryJson(const drogon::orm::Row &row)
{
    Json::Value summary;
    summary["requests"] = static_cast<Json::Int64>(row["requests"].as<long long>());
    summary["promptTokens"] = static_cast<Json::Int64>(row["prompt_tokens"].as<long long>());
    summary["completionTokens"] =
        static_cast<Json::Int64>(row["completion_tokens"].as<long long>());
    summary["totalTokens"] = static_cast<Json::Int64>(row["total_tokens"].as<long long>());
    summary["cacheHitTokens"] =
        static_cast<Json::Int64>(row["cache_hit_tokens"].as<long long>());
    summary["cacheMissTokens"] =
        static_cast<Json::Int64>(row["cache_miss_tokens"].as<long long>());
    return summary;
}

const char *kSummaryColumns =
    "count(*) as requests, "
    "coalesce(sum(prompt_tokens), 0) as prompt_tokens, "
    "coalesce(sum(completion_tokens), 0) as completion_tokens, "
    "coalesce(sum(total_tokens), 0) as total_tokens, "
    "coalesce(sum(cache_hit_tokens), 0) as cache_hit_tokens, "
    "coalesce(sum(cache_miss_tokens), 0) as cache_miss_tokens ";

} // namespace

Task<HttpResponsePtr> AiController::chat(HttpRequestPtr req)
{
    HttpResult result;
    const int userId = CurrentUserId(req);
    if (userId < 0)
    {
        result.setResult(ApiError_TokenInvalidOrExpired);
        co_return JsonResponse(result);
    }

    auto access = TeamService::CheckSubscriptionAccess(userId);
    if (!access.allowed)
    {
        result.setResult(ApiError_UserNotPaid, access.denyMessage);
        co_return JsonResponse(result);
    }

    if (access.hasSubscription && !access.isVendorOrAdmin)
    {
        const int teamId = TeamIdForUser(userId);
        const long long quotaTokens =
            static_cast<long long>(TokenQuotaForPlan(access.plan, access.isTrial)) +
            AddonCreditTokens(teamId);
        const long long usedTokens = MonthlyTeamTokens(userId);
        if (usedTokens >= quotaTokens)
        {
            result.setResult(ApiError_AiQuotaExceeded,
                             "本月 AI 额度已用完（已用 " + std::to_string(usedTokens / 100000) +
                                 " / " + std::to_string(quotaTokens / 100000) +
                                 " 积分），请升级套餐或购买积分包");
            result.jsondata["quota"] =
                QuotaJson(access.plan, access.isTrial, quotaTokens, usedTokens);
            co_return JsonResponse(result);
        }
    }

    const auto config = LoadAiConfig();
    if (!config.enabled || config.baseUrl.empty() || config.apiKey.empty() ||
        config.model.empty())
    {
        result.setResult(ApiError_AiNotConfigured);
        co_return JsonResponse(result);
    }

    auto body = req->getJsonObject();
    if (!body)
    {
        result.setResult(ApiError_InvalidJsonFormat);
        co_return JsonResponse(result);
    }
    const auto &messages = (*body)["messages"];
    if (!messages.isArray() || messages.empty())
    {
        result.setResult(ApiError_MissingRequiredArgs, "缺少 messages");
        co_return JsonResponse(result);
    }
    if (messages.size() > 200)
    {
        result.setResult(ApiError_InvalidOperation, "对话轮次过多，请新建对话");
        co_return JsonResponse(result);
    }

    Json::Value upstream;
    upstream["model"] = config.model;
    upstream["messages"] = messages;
    upstream["stream"] = false;
    upstream["temperature"] = 0.2;
    if ((*body).isMember("tools") && (*body)["tools"].isArray())
    {
        upstream["tools"] = (*body)["tools"];
    }
    if ((*body).isMember("tool_choice"))
    {
        upstream["tool_choice"] = (*body)["tool_choice"];
    }
    if ((*body).isMember("temperature") && (*body)["temperature"].isNumeric())
    {
        const double temperature = (*body)["temperature"].asDouble();
        if (temperature >= 0.0 && temperature <= 2.0)
        {
            upstream["temperature"] = temperature;
        }
    }
    if (config.maxTokens > 0)
    {
        upstream["max_tokens"] = config.maxTokens;
    }

    std::string path;
    auto client = ClientFor(config.baseUrl, path);
    auto upstreamReq = HttpRequest::newHttpJsonRequest(upstream);
    upstreamReq->setMethod(Post);
    upstreamReq->setPath(path);
    upstreamReq->addHeader("Authorization", "Bearer " + config.apiKey);
    upstreamReq->addHeader("Accept", "application/json");

    HttpResponsePtr response;
    try
    {
        response = co_await client->sendRequestCoro(upstreamReq, config.timeoutSec);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "AI 模型请求异常: " << e.what();
        result.setResult(ApiError_AiProviderError, "AI 模型请求失败");
        co_return JsonResponse(result);
    }
    if (!response)
    {
        result.setResult(ApiError_AiProviderError, "AI 模型没有返回结果");
        co_return JsonResponse(result);
    }

    Json::Value payload;
    if (!ParseJson(response->body(), payload))
    {
        result.setResult(ApiError_AiProviderError,
                         "AI 模型返回了非 JSON 数据（HTTP " +
                             std::to_string(response->statusCode()) + "）");
        co_return JsonResponse(result);
    }
    if (response->statusCode() < 200 || response->statusCode() >= 300 ||
        !payload.isMember("choices"))
    {
        const std::string message = ProviderMessage(payload);
        result.setResult(ApiError_AiProviderError,
                         message.empty() ? "AI 模型调用失败" : message);
        result.jsondata = payload;
        co_return JsonResponse(result);
    }

    TokenUsage usage;
    if (ExtractUsage(payload, usage))
    {
        if (usage.model.empty())
        {
            usage.model = config.model;
        }
        RecordUsage(userId, usage);
    }

    result.jsondata = payload;
    result.setResult(ApiError_Success);
    co_return JsonResponse(result);
}

Task<HttpResponsePtr> AiController::usage(HttpRequestPtr req)
{
    HttpResult result;
    const int userId = CurrentUserId(req);
    if (userId < 0)
    {
        result.setResult(ApiError_TokenInvalidOrExpired);
        co_return JsonResponse(result);
    }

    std::string month = req->getParameter("month");
    if (!month.empty() && !ValidMonth(month))
    {
        result.setResult(ApiError_MissingRequiredArgs, "month 格式应为 YYYY-MM");
        co_return JsonResponse(result);
    }

    auto db = drogon::app().getDbClient();
    if (!db)
    {
        result.setResult(ApiError_DatabaseError);
        co_return JsonResponse(result);
    }

    try
    {
        auto nowRows = db->execSqlSync(
            "select to_char(now() at time zone 'Asia/Shanghai', 'YYYY-MM') as month");
        const std::string currentMonth =
            nowRows.empty() ? "" : nowRows[0]["month"].as<std::string>();
        if (month.empty())
        {
            month = currentMonth;
        }

        const std::string bounds =
            " and created_at >= (to_date($2 || '-01', 'YYYY-MM-DD')::timestamp "
            "                    at time zone 'Asia/Shanghai')"
            " and created_at < ((to_date($2 || '-01', 'YYYY-MM-DD') + interval '1 month')::timestamp "
            "                   at time zone 'Asia/Shanghai')";

        auto selfRows = db->execSqlSync(
            std::string("select ") + kSummaryColumns +
                "from ai_usage_log where user_id = $1" + bounds,
            userId, month);
        result.jsondata["month"] = month;
        result.jsondata["self"] =
            selfRows.empty() ? Json::Value(Json::objectValue) : UsageSummaryJson(selfRows[0]);

        auto memberRows = db->execSqlSync(
            "select tm.team_id, r.code as role_code, "
            "       exists(select 1 from user_system_role usr "
            "              join role sr on sr.id = usr.role_id "
            "              where usr.user_id = $1 and sr.code = 'super_admin') as is_super_admin "
            "from team_member tm "
            "join role r on r.id = tm.role_id "
            "where tm.user_id = $1 and tm.status = 1 "
            "order by tm.team_id limit 1",
            userId);
        if (memberRows.empty())
        {
            result.jsondata["teamId"] = Json::Value(Json::nullValue);
            result.setResult(ApiError_Success);
            co_return JsonResponse(result);
        }

        const int teamId = memberRows[0]["team_id"].as<int>();
        const std::string roleCode = memberRows[0]["role_code"].as<std::string>();
        const bool isSuperAdmin = memberRows[0]["is_super_admin"].as<bool>();
        result.jsondata["teamId"] = teamId;

        if (month == currentMonth)
        {
            auto access = TeamService::CheckSubscriptionAccess(userId);
            if (access.hasSubscription && !access.isVendorOrAdmin)
            {
                const long long quotaTokens =
                    static_cast<long long>(TokenQuotaForPlan(access.plan, access.isTrial)) +
                    AddonCreditTokens(teamId);
                const long long usedTokens = MonthlyTeamTokens(userId);
                result.jsondata["quota"] =
                    QuotaJson(access.plan, access.isTrial, quotaTokens, usedTokens);
            }
        }

        if (roleCode != "team_admin" && !isSuperAdmin)
        {
            result.setResult(ApiError_Success);
            co_return JsonResponse(result);
        }

        auto teamRows = db->execSqlSync(
            std::string("select ") + kSummaryColumns +
                "from ai_usage_log where team_id = $1" + bounds,
            teamId, month);
        result.jsondata["team"] =
            teamRows.empty() ? Json::Value(Json::objectValue) : UsageSummaryJson(teamRows[0]);

        auto rows = db->execSqlSync(
            std::string("select l.user_id, u.name, u.nick_name, ") + kSummaryColumns +
                "from ai_usage_log l "
                "join \"user\" u on u.id = l.user_id "
                "where l.team_id = $1"
                "  and l.created_at >= (to_date($2 || '-01', 'YYYY-MM-DD')::timestamp "
                "                       at time zone 'Asia/Shanghai')"
                "  and l.created_at < ((to_date($2 || '-01', 'YYYY-MM-DD') + interval '1 month')::timestamp "
                "                      at time zone 'Asia/Shanghai') "
                "group by l.user_id, u.name, u.nick_name "
                "order by total_tokens desc",
            teamId, month);

        Json::Value members(Json::arrayValue);
        for (const auto &row : rows)
        {
            auto item = UsageSummaryJson(row);
            item["userId"] = row["user_id"].as<int>();
            item["name"] = row["name"].as<std::string>();
            item["nickName"] = row["nick_name"].isNull() ? "" : row["nick_name"].as<std::string>();
            members.append(item);
        }
        result.jsondata["members"] = members;

        result.setResult(ApiError_Success);
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "AI 用量查询失败: " << e.base().what();
        result.setResult(ApiError_DatabaseError);
    }
    co_return JsonResponse(result);
}

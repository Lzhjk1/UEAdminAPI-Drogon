#include "AiController.h"

#include "services/TeamService.h"
#include "utils/AiCrypto.h"
#include "utils/ApiErrorCodes.h"
#include "utils/HttpResult.h"

#include <drogon/HttpClient.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/utils/coroutine.h>
#include <trantor/utils/Logger.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

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
    bool enabled = true;
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

// ---------------------------------------------------------------- AI 服务设置

struct AiTier
{
    std::string model;
    double multiplier = 1.0;
    std::vector<std::string> plans;
};

struct AiSettings
{
    AiTier standard;
    AiTier flagship;
    int globalConcurrency = 32;
    std::map<std::string, int> teamConcurrency;
    int tpm = 200000;
    int queueSeconds = 20;
    std::vector<int> alertThresholds{70, 85, 95};
};

AiSettings DefaultAiSettings()
{
    AiSettings settings;
    const auto config = LoadAiConfig();
    settings.standard.model = config.model;
    settings.standard.multiplier = 1.0;
    settings.standard.plans = {"free", "plus", "trial", "legacy"};
    settings.flagship.model = config.model;
    settings.flagship.multiplier = 2.0;
    settings.flagship.plans = {"pro", "max"};
    settings.teamConcurrency = {{"free", 1}, {"trial", 4}, {"plus", 4},
                                {"pro", 8}, {"max", 16}};
    return settings;
}

std::vector<std::string> StringArray(const Json::Value &node,
                                     const std::vector<std::string> &fallback)
{
    if (!node.isArray())
    {
        return fallback;
    }
    std::vector<std::string> out;
    for (const auto &item : node)
    {
        if (item.isString() && !item.asString().empty())
        {
            out.push_back(item.asString());
        }
    }
    return out.empty() ? fallback : out;
}

AiSettings ParseAiSettings(const Json::Value &data)
{
    AiSettings settings = DefaultAiSettings();
    if (!data.isObject())
    {
        return settings;
    }
    if (data.isMember("tiers") && data["tiers"].isObject())
    {
        const auto &tiers = data["tiers"];
        if (tiers.isMember("standard") && tiers["standard"].isObject())
        {
            const auto &tier = tiers["standard"];
            if (tier.isMember("model") && tier["model"].isString())
            {
                settings.standard.model = tier["model"].asString();
            }
            if (tier.isMember("multiplier") && tier["multiplier"].isNumeric())
            {
                settings.standard.multiplier =
                    std::clamp(tier["multiplier"].asDouble(), 0.1, 10.0);
            }
            settings.standard.plans =
                StringArray(tier["plans"], settings.standard.plans);
        }
        if (tiers.isMember("flagship") && tiers["flagship"].isObject())
        {
            const auto &tier = tiers["flagship"];
            if (tier.isMember("model") && tier["model"].isString())
            {
                settings.flagship.model = tier["model"].asString();
            }
            if (tier.isMember("multiplier") && tier["multiplier"].isNumeric())
            {
                settings.flagship.multiplier =
                    std::clamp(tier["multiplier"].asDouble(), 0.1, 10.0);
            }
            settings.flagship.plans =
                StringArray(tier["plans"], settings.flagship.plans);
        }
    }
    if (data.isMember("concurrency") && data["concurrency"].isObject())
    {
        const auto &concurrency = data["concurrency"];
        if (concurrency.isMember("global") && concurrency["global"].isInt())
        {
            settings.globalConcurrency =
                std::clamp(concurrency["global"].asInt(), 1, 1024);
        }
        if (concurrency.isMember("teamDefault") &&
            concurrency["teamDefault"].isObject())
        {
            for (const auto &name : concurrency["teamDefault"].getMemberNames())
            {
                const auto &value = concurrency["teamDefault"][name];
                if (value.isInt())
                {
                    settings.teamConcurrency[name] =
                        std::clamp(value.asInt(), 0, 256);
                }
            }
        }
        if (concurrency.isMember("tpm") && concurrency["tpm"].isInt())
        {
            settings.tpm = std::clamp(concurrency["tpm"].asInt(), 0, 100000000);
        }
        if (concurrency.isMember("queueSeconds") &&
            concurrency["queueSeconds"].isInt())
        {
            settings.queueSeconds =
                std::clamp(concurrency["queueSeconds"].asInt(), 0, 120);
        }
    }
    if (data.isMember("alerts") && data["alerts"].isObject() &&
        data["alerts"].isMember("thresholds") &&
        data["alerts"]["thresholds"].isArray())
    {
        std::vector<int> thresholds;
        for (const auto &item : data["alerts"]["thresholds"])
        {
            if (item.isInt())
            {
                thresholds.push_back(std::clamp(item.asInt(), 1, 100));
            }
        }
        if (!thresholds.empty())
        {
            settings.alertThresholds = thresholds;
        }
    }
    return settings;
}

Json::Value AiSettingsToJson(const AiSettings &settings)
{
    Json::Value data;
    data["tiers"]["standard"]["model"] = settings.standard.model;
    data["tiers"]["standard"]["multiplier"] = settings.standard.multiplier;
    Json::Value standardPlans(Json::arrayValue);
    for (const auto &plan : settings.standard.plans)
    {
        standardPlans.append(plan);
    }
    data["tiers"]["standard"]["plans"] = standardPlans;
    data["tiers"]["flagship"]["model"] = settings.flagship.model;
    data["tiers"]["flagship"]["multiplier"] = settings.flagship.multiplier;
    Json::Value flagshipPlans(Json::arrayValue);
    for (const auto &plan : settings.flagship.plans)
    {
        flagshipPlans.append(plan);
    }
    data["tiers"]["flagship"]["plans"] = flagshipPlans;
    data["concurrency"]["global"] = settings.globalConcurrency;
    for (const auto &[plan, limit] : settings.teamConcurrency)
    {
        data["concurrency"]["teamDefault"][plan] = limit;
    }
    data["concurrency"]["tpm"] = settings.tpm;
    data["concurrency"]["queueSeconds"] = settings.queueSeconds;
    Json::Value thresholds(Json::arrayValue);
    for (const int value : settings.alertThresholds)
    {
        thresholds.append(value);
    }
    data["alerts"]["thresholds"] = thresholds;
    return data;
}

std::mutex g_settingsMutex;
AiSettings g_settingsCache;
std::chrono::steady_clock::time_point g_settingsAt;
bool g_settingsLoaded = false;

AiSettings LoadAiSettings()
{
    std::lock_guard<std::mutex> lock(g_settingsMutex);
    const auto now = std::chrono::steady_clock::now();
    if (g_settingsLoaded &&
        now - g_settingsAt < std::chrono::seconds(5))
    {
        return g_settingsCache;
    }
    AiSettings settings = DefaultAiSettings();
    try
    {
        auto db = drogon::app().getDbClient();
        if (db)
        {
            auto rows = db->execSqlSync("select data from ai_setting where id = 1");
            if (!rows.empty() && !rows[0]["data"].isNull())
            {
                Json::Value data;
                if (ParseJson(rows[0]["data"].as<std::string>(), data))
                {
                    settings = ParseAiSettings(data);
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        LOG_WARN << "读取 AI 设置失败，使用默认值: " << e.what();
    }
    g_settingsCache = settings;
    g_settingsAt = now;
    g_settingsLoaded = true;
    return settings;
}

void InvalidateAiSettings()
{
    std::lock_guard<std::mutex> lock(g_settingsMutex);
    g_settingsLoaded = false;
}

// ---------------------------------------------------------------- 上游与并发

struct AiUpstream
{
    long long id = 0;
    std::string name;
    std::string kind = "plan";
    std::string baseUrl;
    std::string apiKey;
    std::string keyHint;
    std::string modelStandard;
    std::string modelFlagship;
    int priority = 100;
    bool enabled = true;
    long long monthlyBudget = 0;
    bool byok = false;
};

std::string AiTierForPlan(const AiSettings &settings, const std::string &plan,
                          bool trial)
{
    if (trial)
    {
        return "standard";
    }
    const auto &plans = settings.flagship.plans;
    if (std::find(plans.begin(), plans.end(), plan) != plans.end())
    {
        return "flagship";
    }
    return "standard";
}

std::string AiModelForTier(const AiUpstream &upstream,
                           const AiSettings &settings,
                           const std::string &tier,
                           const std::string &fallback)
{
    std::string model = tier == "flagship" ? upstream.modelFlagship
                                           : upstream.modelStandard;
    if (model.empty())
    {
        model = tier == "flagship" ? settings.flagship.model
                                   : settings.standard.model;
    }
    return model.empty() ? fallback : model;
}

std::vector<AiUpstream> PlatformUpstreamsFromDb(std::string &error)
{
    std::vector<AiUpstream> upstreams;
    std::string secret;
    std::string secretError;
    const bool hasSecret = LoadAiSecret(secret, secretError);
    auto db = drogon::app().getDbClient();
    if (!db)
    {
        return upstreams;
    }
    try
    {
        auto rows = db->execSqlSync(
            "select id, name, kind, base_url, api_key_enc, key_hint, "
            "       model_standard, model_flagship, priority, enabled, monthly_budget "
            "from ai_upstream where enabled = true order by priority, id");
        for (const auto &row : rows)
        {
            AiUpstream upstream;
            upstream.id = row["id"].as<long long>();
            upstream.name = row["name"].as<std::string>();
            upstream.kind = row["kind"].isNull() ? "plan" : row["kind"].as<std::string>();
            upstream.baseUrl = row["base_url"].as<std::string>();
            upstream.keyHint = row["key_hint"].isNull() ? "" : row["key_hint"].as<std::string>();
            upstream.modelStandard = row["model_standard"].isNull()
                                         ? "" : row["model_standard"].as<std::string>();
            upstream.modelFlagship = row["model_flagship"].isNull()
                                         ? "" : row["model_flagship"].as<std::string>();
            upstream.priority = row["priority"].as<int>();
            upstream.enabled = row["enabled"].as<bool>();
            upstream.monthlyBudget = row["monthly_budget"].as<long long>();
            const std::string stored = row["api_key_enc"].as<std::string>();
            if (IsEncryptedAiKey(stored))
            {
                if (!hasSecret || !DecryptAiKey(stored, secret, upstream.apiKey))
                {
                    LOG_WARN << "AI 上游 Key 解密失败，已跳过: " << upstream.name;
                    continue;
                }
            }
            else
            {
                upstream.apiKey = stored;
            }
            if (upstream.baseUrl.empty() || upstream.apiKey.empty())
            {
                continue;
            }
            upstreams.push_back(std::move(upstream));
        }
    }
    catch (const std::exception &e)
    {
        LOG_WARN << "读取 AI 上游失败，回退服务端配置: " << e.what();
        upstreams.clear();
    }
    if (upstreams.empty() && !hasSecret)
    {
        error = secretError;
    }
    return upstreams;
}

std::vector<AiUpstream> LoadAiUpstreams(int teamId, std::string &error)
{
    error.clear();
    if (teamId > 0)
    {
        try
        {
            auto db = drogon::app().getDbClient();
            if (db)
            {
                auto rows = db->execSqlSync(
                    "select team_id, name, base_url, api_key_enc, key_hint, "
                    "       model_standard, model_flagship, enabled "
                    "from team_ai_provider where team_id = $1 and enabled = true",
                    teamId);
                if (!rows.empty())
                {
                    std::string secret;
                    std::string secretError;
                    if (!LoadAiSecret(secret, secretError))
                    {
                        error = secretError;
                        return {};
                    }
                    AiUpstream upstream;
                    upstream.id = -static_cast<long long>(teamId);
                    upstream.name = rows[0]["name"].isNull()
                                        ? "企业自带 Key"
                                        : rows[0]["name"].as<std::string>();
                    upstream.kind = "byok";
                    upstream.byok = true;
                    upstream.baseUrl = rows[0]["base_url"].as<std::string>();
                    upstream.keyHint = rows[0]["key_hint"].isNull()
                                           ? "" : rows[0]["key_hint"].as<std::string>();
                    upstream.modelStandard = rows[0]["model_standard"].isNull()
                                                 ? "" : rows[0]["model_standard"].as<std::string>();
                    upstream.modelFlagship = rows[0]["model_flagship"].isNull()
                                                 ? "" : rows[0]["model_flagship"].as<std::string>();
                    upstream.enabled = rows[0]["enabled"].as<bool>();
                    const std::string stored = rows[0]["api_key_enc"].as<std::string>();
                    if (!DecryptAiKey(stored, secret, upstream.apiKey) ||
                        upstream.apiKey.empty())
                    {
                        error = "企业 AI Key 无法解密，请联系管理员重新绑定";
                        return {};
                    }
                    return {upstream};
                }
            }
        }
        catch (const std::exception &e)
        {
            LOG_WARN << "读取企业 AI Key 失败: " << e.what();
        }
    }
    auto upstreams = PlatformUpstreamsFromDb(error);
    if (!upstreams.empty())
    {
        return upstreams;
    }
    const auto config = LoadAiConfig();
    if (!config.baseUrl.empty() && !config.apiKey.empty() && !config.model.empty())
    {
        AiUpstream upstream;
        upstream.name = "服务端配置";
        upstream.kind = "plan";
        upstream.baseUrl = config.baseUrl;
        upstream.apiKey = config.apiKey;
        upstream.modelStandard = config.model;
        upstream.modelFlagship = config.model;
        upstream.priority = 1;
        upstream.enabled = true;
        upstreams.push_back(std::move(upstream));
    }
    return upstreams;
}

struct AiGateState
{
    std::mutex mutex;
    int globalActive = 0;
    std::map<int, int> teamActive;
    std::map<int, std::deque<std::pair<std::chrono::steady_clock::time_point, int>>> tpm;
};

AiGateState g_aiGate;

int AiGateKey(int teamId, int userId)
{
    return teamId > 0 ? teamId : -userId;
}

bool AiTpmExceeded(int gateKey, int limit)
{
    if (limit <= 0)
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_aiGate.mutex);
    auto &window = g_aiGate.tpm[gateKey];
    const auto cutoff = std::chrono::steady_clock::now() - std::chrono::seconds(60);
    long long used = 0;
    while (!window.empty() && window.front().first < cutoff)
    {
        window.pop_front();
    }
    for (const auto &item : window)
    {
        used += item.second;
    }
    return used >= limit;
}

void AiRecordTpm(int gateKey, int tokens)
{
    if (tokens <= 0)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(g_aiGate.mutex);
    auto &window = g_aiGate.tpm[gateKey];
    window.emplace_back(std::chrono::steady_clock::now(), tokens);
}

bool AiTryAcquire(int gateKey, int teamLimit, int globalLimit)
{
    std::lock_guard<std::mutex> lock(g_aiGate.mutex);
    if (g_aiGate.globalActive >= globalLimit)
    {
        return false;
    }
    if (teamLimit > 0 && g_aiGate.teamActive[gateKey] >= teamLimit)
    {
        return false;
    }
    ++g_aiGate.globalActive;
    ++g_aiGate.teamActive[gateKey];
    return true;
}

void AiRelease(int gateKey)
{
    std::lock_guard<std::mutex> lock(g_aiGate.mutex);
    if (g_aiGate.globalActive > 0)
    {
        --g_aiGate.globalActive;
    }
    auto it = g_aiGate.teamActive.find(gateKey);
    if (it != g_aiGate.teamActive.end())
    {
        if (it->second > 0)
        {
            --it->second;
        }
        if (it->second == 0)
        {
            g_aiGate.teamActive.erase(it);
        }
    }
}

struct AiSlot
{
    int key = 0;
    bool held = false;

    ~AiSlot()
    {
        if (held)
        {
            AiRelease(key);
        }
    }
};

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

void RecordUsage(int userId, const TokenUsage &usage, const AiUpstream &upstream,
                 const std::string &tier)
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
            " cache_hit_tokens, cache_miss_tokens, upstream_id, upstream_name, "
            " provider_kind, model_tier) "
            "values ($1, (select tm.team_id from team_member tm "
            "             where tm.user_id = $2 and tm.status = 1 "
            "             order by tm.team_id limit 1), "
            "        $3, $4, $5, $6, $7, $8, $9, $10, $11, $12)",
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
            usage.cacheMissTokens,
            upstream.id,
            upstream.name,
            upstream.kind,
            tier);
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

double ModelMultiplierFor(const std::string &model)
{
    const auto settings = LoadAiSettings();
    if (!model.empty() && model == settings.standard.model)
    {
        return std::max(0.1, settings.standard.multiplier);
    }
    if (!model.empty() && model == settings.flagship.model)
    {
        return std::max(0.1, settings.flagship.multiplier);
    }
    const auto custom = drogon::app().getCustomConfig();
    if (custom.isMember("AI") && custom["AI"].isObject())
    {
        const auto &ai = custom["AI"];
        if (ai.isMember("ModelMultipliers") && ai["ModelMultipliers"].isObject())
        {
            const auto &multipliers = ai["ModelMultipliers"];
            if (multipliers.isMember(model) && multipliers[model].isNumeric())
            {
                return std::max(0.1, multipliers[model].asDouble());
            }
        }
        if (ai.isMember("DefaultModelMultiplier") && ai["DefaultModelMultiplier"].isNumeric())
        {
            return std::max(0.1, ai["DefaultModelMultiplier"].asDouble());
        }
    }
    return 1.0;
}

long long MonthlyTeamTokens(int userId)
{
    auto db = drogon::app().getDbClient();
    if (!db)
    {
        return 0;
    }
    auto rows = db->execSqlSync(
        "select u.model, coalesce(sum(u.total_tokens), 0) as used "
        "from ai_usage_log u "
        "where u.team_id = (select tm.team_id from team_member tm "
        "                    where tm.user_id = $1 and tm.status = 1 limit 1) "
        "  and u.created_at >= (date_trunc('month', now() at time zone 'Asia/Shanghai') "
        "                       at time zone 'Asia/Shanghai') "
        "  and u.created_at < ((date_trunc('month', now() at time zone 'Asia/Shanghai') "
        "                       + interval '1 month') at time zone 'Asia/Shanghai') "
        "  and (u.provider_kind is null or u.provider_kind <> 'byok') "
        "group by u.model",
        userId);
    long long weighted = 0;
    for (const auto &row : rows)
    {
        const std::string model =
            row["model"].isNull() ? "" : row["model"].as<std::string>();
        const long long used = row["used"].as<long long>();
        weighted += static_cast<long long>(used * ModelMultiplierFor(model) + 0.5);
    }
    return weighted;
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
    if (!config.enabled)
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

    const auto settings = LoadAiSettings();
    const int teamId = TeamIdForUser(userId);
    const int gateKey = AiGateKey(teamId, userId);
    const std::string plan = access.plan.empty() ? "free" : access.plan;
    const std::string tier = AiTierForPlan(settings, plan, access.isTrial);
    int teamLimit = settings.teamConcurrency.count(plan)
                        ? settings.teamConcurrency.at(plan)
                        : 4;
    if (access.isTrial && settings.teamConcurrency.count("trial"))
    {
        teamLimit = settings.teamConcurrency.at("trial");
    }
    if (AiTpmExceeded(gateKey, settings.tpm))
    {
        result.setResult(ApiError_TooManyRequests,
                         "团队 AI 调用频率超限，请稍后再试");
        co_return JsonResponse(result);
    }
    AiSlot slot;
    slot.key = gateKey;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(std::max(0, settings.queueSeconds));
    while (!AiTryAcquire(gateKey, teamLimit, settings.globalConcurrency))
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            result.setResult(ApiError_TooManyRequests, "AI 服务繁忙，请稍后再试");
            co_return JsonResponse(result);
        }
        co_await drogon::sleepCoro(drogon::app().getLoop(), 0.2);
    }
    slot.held = true;

    std::string upstreamError;
    auto upstreams = LoadAiUpstreams(teamId, upstreamError);
    if (upstreams.empty())
    {
        result.setResult(ApiError_AiNotConfigured,
                         upstreamError.empty() ? "AI 服务未配置" : upstreamError);
        co_return JsonResponse(result);
    }

    Json::Value base;
    base["messages"] = messages;
    base["stream"] = false;
    base["temperature"] = 0.2;
    if ((*body).isMember("tools") && (*body)["tools"].isArray())
    {
        base["tools"] = (*body)["tools"];
    }
    if ((*body).isMember("tool_choice"))
    {
        base["tool_choice"] = (*body)["tool_choice"];
    }
    if ((*body).isMember("temperature") && (*body)["temperature"].isNumeric())
    {
        const double temperature = (*body)["temperature"].asDouble();
        if (temperature >= 0.0 && temperature <= 2.0)
        {
            base["temperature"] = temperature;
        }
    }
    if (config.maxTokens > 0)
    {
        base["max_tokens"] = config.maxTokens;
    }

    std::string lastMessage;
    for (const auto &upstreamConfig : upstreams)
    {
        const std::string model =
            AiModelForTier(upstreamConfig, settings, tier, config.model);
        if (model.empty())
        {
            lastMessage = "上游未配置模型";
            continue;
        }
        Json::Value upstream = base;
        upstream["model"] = model;

        std::string path;
        auto client = ClientFor(upstreamConfig.baseUrl, path);
        auto upstreamReq = HttpRequest::newHttpJsonRequest(upstream);
        upstreamReq->setMethod(Post);
        upstreamReq->setPath(path);
        upstreamReq->addHeader("Authorization", "Bearer " + upstreamConfig.apiKey);
        upstreamReq->addHeader("Accept", "application/json");

        HttpResponsePtr response;
        try
        {
            response = co_await client->sendRequestCoro(upstreamReq, config.timeoutSec);
        }
        catch (const std::exception &e)
        {
            LOG_ERROR << "AI 模型请求异常 [" << upstreamConfig.name << "]: " << e.what();
            lastMessage = "AI 模型请求失败：" + std::string(e.what());
            continue;
        }
        if (!response)
        {
            lastMessage = "AI 模型没有返回结果";
            continue;
        }

        Json::Value payload;
        if (!ParseJson(response->body(), payload))
        {
            lastMessage = "AI 模型返回了非 JSON 数据（HTTP " +
                          std::to_string(response->statusCode()) + "）";
            continue;
        }
        if (response->statusCode() < 200 || response->statusCode() >= 300 ||
            !payload.isMember("choices"))
        {
            const std::string message = ProviderMessage(payload);
            lastMessage = message.empty()
                              ? "AI 模型调用失败（HTTP " +
                                    std::to_string(response->statusCode()) + "）"
                              : message;
            LOG_WARN << "AI 上游失败，尝试下一个 [" << upstreamConfig.name
                     << "]: " << lastMessage;
            continue;
        }

        TokenUsage usage;
        if (ExtractUsage(payload, usage))
        {
            if (usage.model.empty())
            {
                usage.model = model;
            }
            RecordUsage(userId, usage, upstreamConfig, tier);
            AiRecordTpm(gateKey, usage.totalTokens);
        }

        result.jsondata = payload;
        result.setResult(ApiError_Success);
        co_return JsonResponse(result);
    }

    result.setResult(ApiError_AiProviderError,
                     lastMessage.empty() ? "AI 模型调用失败" : lastMessage);
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

// ---------------------------------------------------------------- AI 服务管理

namespace
{
const char *kMonthStartSql =
    "(date_trunc('month', now() at time zone 'Asia/Shanghai') "
    " at time zone 'Asia/Shanghai')";
const char *kMonthEndSql =
    "((date_trunc('month', now() at time zone 'Asia/Shanghai') "
    "  + interval '1 month') at time zone 'Asia/Shanghai')";

Json::Value UpstreamAdminJson(const drogon::orm::Row &row)
{
    Json::Value item;
    item["id"] = static_cast<Json::Int64>(row["id"].as<long long>());
    item["name"] = row["name"].as<std::string>();
    item["kind"] = row["kind"].isNull() ? "plan" : row["kind"].as<std::string>();
    item["baseUrl"] = row["base_url"].as<std::string>();
    item["keyHint"] = row["key_hint"].isNull() ? "" : row["key_hint"].as<std::string>();
    item["modelStandard"] = row["model_standard"].isNull()
                                ? "" : row["model_standard"].as<std::string>();
    item["modelFlagship"] = row["model_flagship"].isNull()
                                ? "" : row["model_flagship"].as<std::string>();
    item["priority"] = row["priority"].as<int>();
    item["enabled"] = row["enabled"].as<bool>();
    item["monthlyBudget"] =
        static_cast<Json::Int64>(row["monthly_budget"].as<long long>());
    return item;
}

bool ValidBaseUrl(const std::string &url)
{
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}
} // namespace

Task<HttpResponsePtr> AiController::adminOverview(HttpRequestPtr req)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    if (!db)
    {
        result.setResult(ApiError_DatabaseError);
        co_return JsonResponse(result);
    }
    try
    {
        const auto settings = LoadAiSettings();
        auto totals = db->execSqlSync(
            std::string("select count(*) as requests, "
                        "coalesce(sum(prompt_tokens),0) as prompt_tokens, "
                        "coalesce(sum(completion_tokens),0) as completion_tokens, "
                        "coalesce(sum(total_tokens),0) as total_tokens, "
                        "coalesce(sum(cache_hit_tokens),0) as cache_hit_tokens "
                        "from ai_usage_log where created_at >= ") +
            kMonthStartSql + " and created_at < " + kMonthEndSql +
            " and (provider_kind is null or provider_kind <> 'byok')");
        Json::Value summary(Json::objectValue);
        if (!totals.empty())
        {
            summary["requests"] =
                static_cast<Json::Int64>(totals[0]["requests"].as<long long>());
            summary["promptTokens"] =
                static_cast<Json::Int64>(totals[0]["prompt_tokens"].as<long long>());
            summary["completionTokens"] =
                static_cast<Json::Int64>(totals[0]["completion_tokens"].as<long long>());
            summary["totalTokens"] =
                static_cast<Json::Int64>(totals[0]["total_tokens"].as<long long>());
            summary["cacheHitTokens"] =
                static_cast<Json::Int64>(totals[0]["cache_hit_tokens"].as<long long>());
        }

        auto models = db->execSqlSync(
            std::string("select model, coalesce(sum(total_tokens),0) as used "
                        "from ai_usage_log where created_at >= ") +
            kMonthStartSql + " and created_at < " + kMonthEndSql +
            " and (provider_kind is null or provider_kind <> 'byok') "
            " group by model");
        long long weighted = 0;
        for (const auto &row : models)
        {
            const std::string model =
                row["model"].isNull() ? "" : row["model"].as<std::string>();
            weighted += static_cast<long long>(
                row["used"].as<long long>() * ModelMultiplierFor(model) + 0.5);
        }
        summary["usedCredits"] = static_cast<Json::Int64>(weighted / 100000);

        Json::Value upstreams(Json::arrayValue);
        auto rows = db->execSqlSync(
            "select id, name, kind, base_url, key_hint, model_standard, "
            "       model_flagship, priority, enabled, monthly_budget "
            "from ai_upstream order by priority, id");
        for (const auto &row : rows)
        {
            upstreams.append(UpstreamAdminJson(row));
        }

        int active = 0;
        {
            std::lock_guard<std::mutex> lock(g_aiGate.mutex);
            active = g_aiGate.globalActive;
        }
        result.jsondata["totals"] = summary;
        result.jsondata["upstreams"] = upstreams;
        result.jsondata["settings"] = AiSettingsToJson(settings);
        result.jsondata["concurrency"]["active"] = active;
        result.jsondata["concurrency"]["limit"] = settings.globalConcurrency;
        result.setResult(ApiError_Success);
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "AI 管理概览查询失败: " << e.base().what();
        result.setResult(ApiError_DatabaseError);
    }
    co_return JsonResponse(result);
}

Task<HttpResponsePtr> AiController::adminSettings(HttpRequestPtr req)
{
    HttpResult result;
    result.jsondata = AiSettingsToJson(LoadAiSettings());
    result.setResult(ApiError_Success);
    co_return JsonResponse(result);
}

Task<HttpResponsePtr> AiController::adminSaveSettings(HttpRequestPtr req)
{
    HttpResult result;
    auto body = req->getJsonObject();
    if (!body)
    {
        result.setResult(ApiError_InvalidJsonFormat);
        co_return JsonResponse(result);
    }
    const AiSettings settings = ParseAiSettings(*body);
    auto db = drogon::app().getDbClient();
    if (!db)
    {
        result.setResult(ApiError_DatabaseError);
        co_return JsonResponse(result);
    }
    try
    {
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        const std::string text = Json::writeString(builder, AiSettingsToJson(settings));
        db->execSqlSync(
            "insert into ai_setting (id, data, updated_at) values (1, $1::jsonb, now()) "
            "on conflict (id) do update set data = excluded.data, updated_at = now()",
            text);
        InvalidateAiSettings();
        result.jsondata = AiSettingsToJson(settings);
        result.setResult(ApiError_Success);
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "保存 AI 设置失败: " << e.base().what();
        result.setResult(ApiError_DatabaseError);
    }
    co_return JsonResponse(result);
}

Task<HttpResponsePtr> AiController::adminUpstreams(HttpRequestPtr req)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    if (!db)
    {
        result.setResult(ApiError_DatabaseError);
        co_return JsonResponse(result);
    }
    try
    {
        Json::Value items(Json::arrayValue);
        auto rows = db->execSqlSync(
            "select id, name, kind, base_url, key_hint, model_standard, "
            "       model_flagship, priority, enabled, monthly_budget "
            "from ai_upstream order by priority, id");
        for (const auto &row : rows)
        {
            items.append(UpstreamAdminJson(row));
        }
        result.jsondata["items"] = items;
        result.setResult(ApiError_Success);
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "读取 AI 上游失败: " << e.base().what();
        result.setResult(ApiError_DatabaseError);
    }
    co_return JsonResponse(result);
}

Task<HttpResponsePtr> AiController::adminSaveUpstream(HttpRequestPtr req)
{
    HttpResult result;
    auto body = req->getJsonObject();
    if (!body)
    {
        result.setResult(ApiError_InvalidJsonFormat);
        co_return JsonResponse(result);
    }
    const int id = body->get("id", 0).asInt();
    const std::string name = body->get("name", "").asString();
    const std::string kind = body->get("kind", "plan").asString();
    const std::string baseUrl = body->get("baseUrl", "").asString();
    const std::string apiKey = body->get("apiKey", "").asString();
    const std::string modelStandard = body->get("modelStandard", "").asString();
    const std::string modelFlagship = body->get("modelFlagship", "").asString();
    const int priority = std::clamp(body->get("priority", 100).asInt(), 1, 999);
    const bool enabled = body->get("enabled", true).asBool();
    const long long monthlyBudget =
        std::max<long long>(0, body->get("monthlyBudget", 0).asInt64());
    if (name.empty() || !ValidBaseUrl(baseUrl))
    {
        result.setResult(ApiError_MissingRequiredArgs, "名称和 http(s) 地址必填");
        co_return JsonResponse(result);
    }
    if (kind != "plan" && kind != "payg")
    {
        result.setResult(ApiError_InvalidOperation, "上游类型只能是 plan 或 payg");
        co_return JsonResponse(result);
    }
    if (id <= 0 && apiKey.empty())
    {
        result.setResult(ApiError_MissingRequiredArgs, "新增上游必须填写 API Key");
        co_return JsonResponse(result);
    }
    auto db = drogon::app().getDbClient();
    if (!db)
    {
        result.setResult(ApiError_DatabaseError);
        co_return JsonResponse(result);
    }
    std::string keyEnc;
    std::string keyHint;
    if (!apiKey.empty())
    {
        std::string secret;
        std::string error;
        if (!LoadAiSecret(secret, error))
        {
            result.setResult(ApiError_InvalidOperation, error);
            co_return JsonResponse(result);
        }
        keyEnc = EncryptAiKey(apiKey, secret);
        if (keyEnc.empty())
        {
            result.setResult(ApiError_InternalError, "加密 API Key 失败");
            co_return JsonResponse(result);
        }
        keyHint = MaskAiKey(apiKey);
    }
    try
    {
        long long savedId = id;
        if (id > 0)
        {
            if (keyEnc.empty())
            {
                db->execSqlSync(
                    "update ai_upstream set name=$1, kind=$2, base_url=$3, "
                    " model_standard=$4, model_flagship=$5, priority=$6, "
                    " enabled=$7, monthly_budget=$8, updated_at=now() where id=$9::bigint",
                    name, kind, baseUrl, modelStandard, modelFlagship, priority,
                    enabled, monthlyBudget, static_cast<long long>(id));
            }
            else
            {
                db->execSqlSync(
                    "update ai_upstream set name=$1, kind=$2, base_url=$3, "
                    " model_standard=$4, model_flagship=$5, priority=$6, "
                    " enabled=$7, monthly_budget=$8, api_key_enc=$9, key_hint=$10, "
                    " updated_at=now() where id=$11::bigint",
                    name, kind, baseUrl, modelStandard, modelFlagship, priority,
                    enabled, monthlyBudget, keyEnc, keyHint, static_cast<long long>(id));
            }
        }
        else
        {
            auto rows = db->execSqlSync(
                "insert into ai_upstream (name, kind, base_url, api_key_enc, key_hint, "
                " model_standard, model_flagship, priority, enabled, monthly_budget) "
                "values ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10) returning id",
                name, kind, baseUrl, keyEnc, keyHint, modelStandard, modelFlagship,
                priority, enabled, monthlyBudget);
            if (!rows.empty())
            {
                savedId = rows[0]["id"].as<long long>();
            }
        }
        result.jsondata["id"] = static_cast<Json::Int64>(savedId);
        result.setResult(ApiError_Success);
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "保存 AI 上游失败: " << e.base().what();
        result.setResult(ApiError_DatabaseError);
    }
    co_return JsonResponse(result);
}

Task<HttpResponsePtr> AiController::adminDeleteUpstream(HttpRequestPtr req)
{
    HttpResult result;
    auto body = req->getJsonObject();
    const int id = body ? body->get("id", 0).asInt() : 0;
    if (id <= 0)
    {
        result.setResult(ApiError_MissingRequiredArgs, "缺少上游 id");
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
        db->execSqlSync("delete from ai_upstream where id = $1::bigint",
                        static_cast<long long>(id));
        result.setResult(ApiError_Success);
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "删除 AI 上游失败: " << e.base().what();
        result.setResult(ApiError_DatabaseError);
    }
    co_return JsonResponse(result);
}

Task<HttpResponsePtr> AiController::adminTeams(HttpRequestPtr req)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    if (!db)
    {
        result.setResult(ApiError_DatabaseError);
        co_return JsonResponse(result);
    }
    try
    {
        auto rows = db->execSqlSync(
            std::string(
                "select t.id, t.name, "
                "  (select count(*) from team_member tm where tm.team_id=t.id and tm.status=1) as members, "
                "  s.plan, coalesce(s.is_trial,false) as is_trial, "
                "  to_char(s.end_date,'YYYY-MM-DD') as end_date, "
                "  (select coalesce(sum(u.total_tokens),0) from ai_usage_log u "
                "    where u.team_id=t.id and u.created_at >= ") + kMonthStartSql +
                " and u.created_at < " + kMonthEndSql + ") as used_tokens, "
                "  exists(select 1 from team_ai_provider p where p.team_id=t.id and p.enabled) as byok, "
                "  coalesce((select p.key_hint from team_ai_provider p where p.team_id=t.id),'') as key_hint, "
                "  coalesce((select p.name from team_ai_provider p where p.team_id=t.id),'') as byok_name "
                "from team t "
                "left join lateral (select plan,is_trial,end_date from team_subscription ts "
                "  where ts.team_id=t.id and ts.status=1 and ts.product='ueaicad' "
                "  order by ts.end_date desc limit 1) s on true "
                "where t.status=1 order by used_tokens desc, t.id limit 200");
        Json::Value items(Json::arrayValue);
        for (const auto &row : rows)
        {
            const std::string plan =
                row["plan"].isNull() ? "free" : row["plan"].as<std::string>();
            const bool trial = row["is_trial"].as<bool>();
            const long long used = row["used_tokens"].as<long long>();
            const long long quota = TokenQuotaForPlan(plan, trial);
            Json::Value item;
            item["teamId"] = row["id"].as<int>();
            item["name"] = row["name"].as<std::string>();
            item["members"] = row["members"].as<int>();
            item["plan"] = plan;
            item["trial"] = trial;
            item["endDate"] = row["end_date"].isNull() ? "" : row["end_date"].as<std::string>();
            item["usedTokens"] = static_cast<Json::Int64>(used);
            item["monthlyTokens"] = static_cast<Json::Int64>(quota);
            item["usedCredits"] = static_cast<Json::Int64>(used / 100000);
            item["monthlyCredits"] = static_cast<Json::Int64>(quota / 100000);
            item["byok"] = row["byok"].as<bool>();
            item["keyHint"] = row["key_hint"].as<std::string>();
            item["byokName"] = row["byok_name"].as<std::string>();
            item["source"] = row["byok"].as<bool>() ? "byok" : "platform";
            item["status"] = (quota > 0 && used >= quota) ? "exhausted" : "ok";
            items.append(item);
        }
        result.jsondata["items"] = items;
        result.setResult(ApiError_Success);
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "读取企业 AI 列表失败: " << e.base().what();
        result.setResult(ApiError_DatabaseError);
    }
    co_return JsonResponse(result);
}

Task<HttpResponsePtr> AiController::adminBindByok(HttpRequestPtr req)
{
    HttpResult result;
    auto body = req->getJsonObject();
    if (!body)
    {
        result.setResult(ApiError_InvalidJsonFormat);
        co_return JsonResponse(result);
    }
    const int teamId = body->get("teamId", 0).asInt();
    const std::string name = body->get("name", "").asString();
    const std::string baseUrl = body->get("baseUrl", "").asString();
    const std::string apiKey = body->get("apiKey", "").asString();
    const std::string modelStandard = body->get("modelStandard", "").asString();
    const std::string modelFlagship = body->get("modelFlagship", "").asString();
    const bool enabled = body->get("enabled", true).asBool();
    if (teamId <= 0 || !ValidBaseUrl(baseUrl))
    {
        result.setResult(ApiError_MissingRequiredArgs, "企业团队和 http(s) 地址必填");
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
        auto team = db->execSqlSync("select 1 from team where id = $1 and status = 1", teamId);
        if (team.empty())
        {
            result.setResult(ApiError_TeamNotFound);
            co_return JsonResponse(result);
        }
        auto existing = db->execSqlSync(
            "select 1 from team_ai_provider where team_id = $1", teamId);
        if (existing.empty() && apiKey.empty())
        {
            result.setResult(ApiError_MissingRequiredArgs, "首次绑定必须填写 API Key");
            co_return JsonResponse(result);
        }
        std::string keyEnc;
        std::string keyHint;
        if (!apiKey.empty())
        {
            std::string secret;
            std::string error;
            if (!LoadAiSecret(secret, error))
            {
                result.setResult(ApiError_InvalidOperation, error);
                co_return JsonResponse(result);
            }
            keyEnc = EncryptAiKey(apiKey, secret);
            if (keyEnc.empty())
            {
                result.setResult(ApiError_InternalError, "加密 API Key 失败");
                co_return JsonResponse(result);
            }
            keyHint = MaskAiKey(apiKey);
        }
        if (existing.empty())
        {
            db->execSqlSync(
                "insert into team_ai_provider (team_id, name, base_url, api_key_enc, "
                " key_hint, model_standard, model_flagship, enabled) "
                "values ($1,$2,$3,$4,$5,$6,$7,$8)",
                teamId, name, baseUrl, keyEnc, keyHint, modelStandard, modelFlagship,
                enabled);
        }
        else if (keyEnc.empty())
        {
            db->execSqlSync(
                "update team_ai_provider set name=$1, base_url=$2, "
                " model_standard=$3, model_flagship=$4, enabled=$5, updated_at=now() "
                "where team_id=$6",
                name, baseUrl, modelStandard, modelFlagship, enabled, teamId);
        }
        else
        {
            db->execSqlSync(
                "update team_ai_provider set name=$1, base_url=$2, api_key_enc=$3, "
                " key_hint=$4, model_standard=$5, model_flagship=$6, enabled=$7, "
                " updated_at=now() where team_id=$8",
                name, baseUrl, keyEnc, keyHint, modelStandard, modelFlagship,
                enabled, teamId);
        }
        result.setResult(ApiError_Success);
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "绑定企业 AI Key 失败: " << e.base().what();
        result.setResult(ApiError_DatabaseError);
    }
    co_return JsonResponse(result);
}

Task<HttpResponsePtr> AiController::adminRemoveByok(HttpRequestPtr req)
{
    HttpResult result;
    auto body = req->getJsonObject();
    const int teamId = body ? body->get("teamId", 0).asInt() : 0;
    if (teamId <= 0)
    {
        result.setResult(ApiError_MissingRequiredArgs, "缺少企业团队");
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
        db->execSqlSync("delete from team_ai_provider where team_id = $1", teamId);
        result.setResult(ApiError_Success);
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "解绑企业 AI Key 失败: " << e.base().what();
        result.setResult(ApiError_DatabaseError);
    }
    co_return JsonResponse(result);
}

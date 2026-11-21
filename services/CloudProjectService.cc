#include "CloudProjectService.h"

#include "utils/CosSigner.h"

#include <drogon/drogon.h>
#include <drogon/orm/DbClient.h>
#include <drogon/orm/Row.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <string>

using namespace drogon;
using namespace UEAdminAPI::utils;

namespace UEAdminAPI {
namespace Services {

namespace
{

struct TeamContext
{
    int userId = 0;
    int teamId = 0;
    std::string roleCode;
    std::string plan;
    bool hasSubscription = false;
    bool active = false;
    bool isVendor = false;
    bool isSuperAdmin = false;

    bool IsTeamAdmin() const
    {
        return roleCode == "team_admin" || isSuperAdmin;
    }
};

long long NowUnix()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string Trim(const std::string &value)
{
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])))
    {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
    {
        --end;
    }
    return value.substr(begin, end - begin);
}

bool HasUnsafeChars(const std::string &value)
{
    for (const unsigned char c : value)
    {
        if (c < 0x20 || c == 0x7F)
        {
            return true;
        }
        switch (c)
        {
        case '/':
        case '\\':
        case ':':
        case '*':
        case '?':
        case '"':
        case '<':
        case '>':
        case '|':
            return true;
        default:
            break;
        }
    }
    return false;
}

bool NormalizeProjectName(const std::string &input, std::string &out, std::string &err)
{
    out = Trim(input);
    if (out.empty())
    {
        err = "缺少 projectName";
        return false;
    }
    if (out.size() > 200)
    {
        err = "工程名过长";
        return false;
    }
    if (HasUnsafeChars(out) || out == "." || out == "..")
    {
        err = "工程名包含非法字符";
        return false;
    }
    return true;
}

bool NormalizeRelativePath(const std::string &input, std::string &out, std::string &err)
{
    std::string raw = Trim(input);
    if (raw.empty())
    {
        err = "缺少 relativePath";
        return false;
    }
    if (raw.size() > 512)
    {
        err = "文件路径过长";
        return false;
    }
    for (auto &c : raw)
    {
        if (c == '\\')
        {
            c = '/';
        }
    }
    while (!raw.empty() && raw.front() == '/')
    {
        raw.erase(raw.begin());
    }
    std::string normalized;
    size_t start = 0;
    while (start <= raw.size())
    {
        const size_t slash = raw.find('/', start);
        const std::string part =
            raw.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (part.empty() || part == "." || part == "..")
        {
            err = "文件路径不合法";
            return false;
        }
        if (HasUnsafeChars(part))
        {
            err = "文件路径包含非法字符";
            return false;
        }
        if (!normalized.empty())
        {
            normalized.push_back('/');
        }
        normalized += part;
        if (slash == std::string::npos)
        {
            break;
        }
        start = slash + 1;
    }

    std::string lower = normalized;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool allowed =
        (lower.size() >= 3 && lower.compare(lower.size() - 3, 3, ".db") == 0) ||
        (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".mdb") == 0);
    if (!allowed)
    {
        err = "只允许同步 .db / .mdb 工程文件";
        return false;
    }
    out = normalized;
    return true;
}

bool NormalizeSha256(const std::string &input, std::string &out, std::string &err)
{
    out.clear();
    if (input.empty())
    {
        return true;
    }
    if (input.size() != 64)
    {
        err = "sha256 格式不正确";
        return false;
    }
    for (const unsigned char c : input)
    {
        if (!std::isxdigit(c))
        {
            err = "sha256 格式不正确";
            return false;
        }
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return true;
}

std::string CosHost(const CloudConfig &config)
{
    if (!config.endpoint.empty())
    {
        return config.endpoint;
    }
    return config.bucket + ".cos." + config.region + ".myqcloud.com";
}

bool CosConfigured(const CloudConfig &config)
{
    return !config.secretId.empty() && !config.secretKey.empty() && !config.bucket.empty() &&
           !config.region.empty();
}

std::string RandomHex(size_t bytes)
{
    static const char *kHex = "0123456789abcdef";
    std::random_device rd;
    std::string out;
    out.reserve(bytes * 2);
    for (size_t i = 0; i < bytes; ++i)
    {
        const unsigned char value = static_cast<unsigned char>(rd() & 0xFF);
        out.push_back(kHex[value >> 4]);
        out.push_back(kHex[value & 0x0F]);
    }
    return out;
}

std::string LocalSecret(const CloudConfig &config)
{
    static std::string secret;
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    if (secret.empty())
    {
        secret = config.localSecret.empty() ? RandomHex(32) : config.localSecret;
    }
    return secret;
}

std::string LocalToken(const CloudConfig &config,
                       const std::string &objectKey,
                       const std::string &method,
                       long long exp)
{
    return CosSigner::HmacSha256Hex(LocalSecret(config),
                                    method + "\n" + objectKey + "\n" + std::to_string(exp));
}

int QuotaGb(const TeamContext &context)
{
    if (context.isVendor || context.isSuperAdmin)
    {
        return 200;
    }
    if (context.plan == "max")
    {
        return 200;
    }
    if (context.plan == "pro" || context.plan == "standard" || context.plan == "legacy")
    {
        return 50;
    }
    if (context.plan == "plus" || context.plan == "trial")
    {
        return 10;
    }
    return 50; // 无订阅记录的存量团队按 Pro 级
}

bool Writable(const TeamContext &context)
{
    if (context.isVendor || context.isSuperAdmin)
    {
        return true;
    }
    if (!context.hasSubscription)
    {
        return true; // 存量授权
    }
    return context.active;
}

bool LoadTeamContext(int userId, TeamContext &context, std::string &err)
{
    auto db = drogon::app().getDbClient();
    auto rows = db->execSqlSync(
        "select t.id as team_id, coalesce(t.is_vendor, false) as is_vendor, "
        "       r.code as role_code, "
        "       exists(select 1 from user_system_role usr "
        "              join role sr on sr.id = usr.role_id "
        "              where usr.user_id = $1 and sr.code = 'super_admin') as is_super_admin, "
        "       s.plan, (s.end_date is not null) as has_sub, "
        "       coalesce(s.end_date >= (now() at time zone 'Asia/Shanghai')::date, false) as sub_active "
        "from team_member tm "
        "join team t on t.id = tm.team_id "
        "join role r on r.id = tm.role_id "
        "left join lateral (select plan, end_date from team_subscription ts "
        "                   where ts.team_id = t.id and ts.status = 1 and ts.product = 'ueaicad' "
        "                   order by ts.end_date desc limit 1) s on true "
        "where tm.user_id = $1 and tm.status = 1 "
        "order by tm.team_id limit 1",
        userId);
    if (rows.empty())
    {
        err = "用户不属于任何团队";
        return false;
    }
    const auto &row = rows[0];
    context.userId = userId;
    context.teamId = row["team_id"].as<int>();
    context.isVendor = row["is_vendor"].as<bool>();
    context.roleCode = row["role_code"].as<std::string>();
    context.isSuperAdmin = row["is_super_admin"].as<bool>();
    context.plan = row["plan"].isNull() ? "" : row["plan"].as<std::string>();
    context.hasSubscription = row["has_sub"].as<bool>();
    context.active = row["sub_active"].as<bool>();
    return true;
}

long long UsedBytes(int teamId)
{
    auto db = drogon::app().getDbClient();
    auto rows = db->execSqlSync(
        "select coalesce(sum(file_size), 0)::bigint as used "
        "from cloud_project_file where team_id = $1",
        teamId);
    return rows.empty() ? 0 : rows[0]["used"].as<long long>();
}

long long ExistingFileSize(int teamId, const std::string &objectKey)
{
    auto db = drogon::app().getDbClient();
    auto rows = db->execSqlSync(
        "select file_size from cloud_project_file where team_id = $1 and object_key = $2",
        teamId, objectKey);
    return rows.empty() ? 0 : rows[0]["file_size"].as<long long>();
}

std::string Mb(long long bytes)
{
    return std::to_string((bytes + 1024 * 1024 - 1) / (1024 * 1024));
}

bool CosObjectExists(const CloudConfig &config, const std::string &objectKey, std::string &err)
{
    const std::string host = CosHost(config);
    const std::string rawPath = "/" + objectKey;
    auto client = HttpClient::newHttpClient("https://" + host);
    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Head);
    req->setPath(CosSigner::EncodePath(rawPath));
    req->addHeader("Authorization",
                   CosSigner::Authorization(config.secretId, config.secretKey, "HEAD", rawPath,
                                            host, NowUnix(), config.urlExpireSec));
    auto [result, resp] = client->sendRequest(req, 15.0);
    if (result != ReqResult::Ok || !resp)
    {
        err = "无法连接云存储";
        return false;
    }
    if (resp->statusCode() == k404NotFound)
    {
        err = "云端对象不存在，请先完成上传";
        return false;
    }
    if (resp->statusCode() < 200 || resp->statusCode() >= 300)
    {
        err = "云存储校验失败（HTTP " + std::to_string(resp->statusCode()) + "）";
        return false;
    }
    return true;
}

bool CosDeleteObject(const CloudConfig &config, const std::string &objectKey, std::string &err)
{
    const std::string host = CosHost(config);
    const std::string rawPath = "/" + objectKey;
    auto client = HttpClient::newHttpClient("https://" + host);
    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Delete);
    req->setPath(CosSigner::EncodePath(rawPath));
    req->addHeader("Authorization",
                   CosSigner::Authorization(config.secretId, config.secretKey, "DELETE", rawPath,
                                            host, NowUnix(), config.urlExpireSec));
    auto [result, resp] = client->sendRequest(req, 15.0);
    if (result != ReqResult::Ok || !resp)
    {
        err = "无法连接云存储";
        return false;
    }
    if (resp->statusCode() == k404NotFound)
    {
        return true;
    }
    if (resp->statusCode() < 200 || resp->statusCode() >= 300)
    {
        err = "云端删除失败（HTTP " + std::to_string(resp->statusCode()) + "）";
        return false;
    }
    return true;
}

Json::Value FileJson(const drogon::orm::Row &row)
{
    Json::Value item;
    item["id"] = static_cast<Json::Int64>(row["id"].as<long long>());
    item["projectName"] = row["project_name"].as<std::string>();
    item["relativePath"] = row["relative_path"].as<std::string>();
    item["size"] = static_cast<Json::Int64>(row["file_size"].as<long long>());
    item["sha256"] = row["sha256"].isNull() ? "" : row["sha256"].as<std::string>();
    item["updatedAt"] = row["updated_at"].isNull() ? "" : row["updated_at"].as<std::string>();
    return item;
}

} // namespace

CloudConfig CloudProjectService::LoadConfig()
{
    CloudConfig config;
    const auto custom = drogon::app().getCustomConfig();
    if (!custom.isMember("CloudProject") || !custom["CloudProject"].isObject())
    {
        return config;
    }
    const auto &cp = custom["CloudProject"];
    config.enabled = cp.get("Enabled", false).asBool();
    if (cp.isMember("Driver") && cp["Driver"].isString())
    {
        config.driver = cp["Driver"].asString();
    }
    config.urlExpireSec = cp.get("UrlExpireSec", 900).asInt();
    if (config.urlExpireSec < 60)
    {
        config.urlExpireSec = 60;
    }
    config.maxFileSizeBytes = cp.get("MaxFileSizeMb", 512).asInt64() * 1024 * 1024;
    if (config.maxFileSizeBytes <= 0)
    {
        config.maxFileSizeBytes = 512LL * 1024 * 1024;
    }
    config.keyPrefix = cp.get("KeyPrefix", "cloud-projects").asString();
    if (config.keyPrefix.empty())
    {
        config.keyPrefix = "cloud-projects";
    }
    config.localStoreDir = cp.get("LocalStoreDir", "./uploads/cloud").asString();
    config.localSecret = cp.get("LocalSecret", "").asString();

    if (cp.isMember("Cos") && cp["Cos"].isObject())
    {
        const auto &cos = cp["Cos"];
        config.secretId = cos.get("SecretId", "").asString();
        config.secretKey = cos.get("SecretKey", "").asString();
        config.bucket = cos.get("Bucket", "").asString();
        config.region = cos.get("Region", "ap-shanghai").asString();
        config.endpoint = cos.get("Endpoint", "").asString();
    }
    return config;
}

long long CloudProjectService::MaxFileSizeBytes()
{
    return LoadConfig().maxFileSizeBytes;
}

bool CloudProjectService::LocalPathFor(const std::string &objectKey, std::string &path)
{
    const auto config = LoadConfig();
    if (config.driver != "local" || objectKey.empty())
    {
        return false;
    }
    // 落盘名用哈希，避免中文与路径穿越问题；真实 key 只存元数据。
    const std::string name = CosSigner::Sha1Hex(objectKey) + ".bin";
    std::error_code ec;
    const auto root = std::filesystem::absolute(std::filesystem::path(config.localStoreDir), ec);
    if (ec)
    {
        return false;
    }
    std::filesystem::create_directories(root, ec);
    path = (root / name).string();
    return !ec;
}

bool CloudProjectService::VerifyLocalToken(const std::string &objectKey,
                                           const std::string &method,
                                           long long exp,
                                           const std::string &token)
{
    const auto config = LoadConfig();
    if (config.driver != "local" || token.empty() || exp < NowUnix())
    {
        return false;
    }
    return LocalToken(config, objectKey, method, exp) == token;
}

utils::HttpResult CloudProjectService::Status(int userId)
{
    utils::HttpResult result;
    const auto config = LoadConfig();
    if (!config.enabled)
    {
        result.jsondata["enabled"] = false;
        result.setResult(ApiError_Success);
        return result;
    }

    try
    {
        TeamContext context;
        std::string err;
        if (!LoadTeamContext(userId, context, err))
        {
            result.setResult(ApiError_NotTeamMember, err);
            return result;
        }

        auto db = drogon::app().getDbClient();
        auto usedRows = db->execSqlSync(
            "select coalesce(sum(file_size), 0)::bigint as used, count(*) as files "
            "from cloud_project_file where team_id = $1",
            context.teamId);
        const long long used = usedRows.empty() ? 0 : usedRows[0]["used"].as<long long>();
        const long long files = usedRows.empty() ? 0 : usedRows[0]["files"].as<long long>();

        auto projectRows = db->execSqlSync(
            "select project_name, count(*) as files, coalesce(sum(file_size), 0)::bigint as bytes, "
            "       to_char(max(updated_at) at time zone 'Asia/Shanghai', "
            "               'YYYY-MM-DD\"T\"HH24:MI:SS') as updated_at "
            "from cloud_project_file where team_id = $1 "
            "group by project_name order by max(updated_at) desc",
            context.teamId);

        Json::Value projects(Json::arrayValue);
        for (const auto &row : projectRows)
        {
            Json::Value project;
            project["name"] = row["project_name"].as<std::string>();
            project["files"] = static_cast<Json::Int64>(row["files"].as<long long>());
            project["bytes"] = static_cast<Json::Int64>(row["bytes"].as<long long>());
            project["updatedAt"] =
                row["updated_at"].isNull() ? "" : row["updated_at"].as<std::string>();
            projects.append(project);
        }

        const int quotaGb = QuotaGb(context);
        result.jsondata["enabled"] = true;
        result.jsondata["readOnly"] = !Writable(context);
        result.jsondata["plan"] = context.plan;
        result.jsondata["quotaGb"] = quotaGb;
        result.jsondata["quotaBytes"] =
            static_cast<Json::Int64>(static_cast<long long>(quotaGb) * 1024 * 1024 * 1024);
        result.jsondata["usedBytes"] = static_cast<Json::Int64>(used);
        result.jsondata["fileCount"] = static_cast<Json::Int64>(files);
        result.jsondata["maxFileSizeMb"] =
            static_cast<Json::Int64>(config.maxFileSizeBytes / (1024 * 1024));
        result.jsondata["projects"] = projects;
        result.setResult(ApiError_Success);
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "云工程状态查询失败: " << e.base().what();
        result.setResult(ApiError_DatabaseError);
    }
    return result;
}

utils::HttpResult CloudProjectService::Prepare(int userId,
                                               const Json::Value &body,
                                               const std::string &baseUrl)
{
    utils::HttpResult result;
    const auto config = LoadConfig();
    if (!config.enabled)
    {
        result.setResult(ApiError_InvalidOperation, "云工程未开通");
        return result;
    }
    if (config.driver == "cos" && !CosConfigured(config))
    {
        result.setResult(ApiError_InvalidOperation, "云存储未配置");
        return result;
    }

    std::string projectName;
    std::string relativePath;
    std::string sha256;
    std::string err;
    if (!NormalizeProjectName(body.get("projectName", "").asString(), projectName, err) ||
        !NormalizeRelativePath(body.get("relativePath", "").asString(), relativePath, err) ||
        !NormalizeSha256(body.get("sha256", "").asString(), sha256, err))
    {
        result.setResult(ApiError_MissingRequiredArgs, err);
        return result;
    }
    const long long size = body.get("size", 0).asInt64();
    if (size <= 0 || size > config.maxFileSizeBytes)
    {
        result.setResult(ApiError_InvalidOperation, "文件大小不合法");
        return result;
    }

    try
    {
        TeamContext context;
        if (!LoadTeamContext(userId, context, err))
        {
            result.setResult(ApiError_NotTeamMember, err);
            return result;
        }
        if (!Writable(context))
        {
            result.setResult(ApiError_UserNotPaid, "订阅已到期，云工程只读");
            return result;
        }

        const std::string objectKey =
            config.keyPrefix + "/" + std::to_string(context.teamId) + "/" + projectName + "/" +
            relativePath;
        const long long existing = ExistingFileSize(context.teamId, objectKey);
        const long long used = UsedBytes(context.teamId);
        const long long quota = static_cast<long long>(QuotaGb(context)) * 1024 * 1024 * 1024;
        if (used - existing + size > quota)
        {
            result.setResult(ApiError_InvalidOperation,
                             "云工程空间不足：已用 " + Mb(used) + " MB / 配额 " +
                                 std::to_string(QuotaGb(context)) + " GB");
            return result;
        }

        std::string uploadUrl;
        if (config.driver == "local")
        {
            const long long exp = NowUnix() + config.urlExpireSec;
            uploadUrl = baseUrl + "/api/cloud/direct?method=PUT&key=" +
                        CosSigner::EncodeValue(objectKey) + "&exp=" + std::to_string(exp) +
                        "&token=" + LocalToken(config, objectKey, "PUT", exp);
        }
        else
        {
            uploadUrl = CosSigner::PresignUrl("https", CosHost(config), objectKey,
                                              config.secretId, config.secretKey, "PUT",
                                              NowUnix(), config.urlExpireSec);
        }

        result.jsondata["objectKey"] = objectKey;
        result.jsondata["projectName"] = projectName;
        result.jsondata["relativePath"] = relativePath;
        result.jsondata["method"] = "PUT";
        result.jsondata["uploadUrl"] = uploadUrl;
        result.jsondata["expiresIn"] = config.urlExpireSec;
        result.setResult(ApiError_Success);
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "云工程上传准备失败: " << e.base().what();
        result.setResult(ApiError_DatabaseError);
    }
    return result;
}

utils::HttpResult CloudProjectService::Complete(int userId, const Json::Value &body)
{
    utils::HttpResult result;
    const auto config = LoadConfig();
    if (!config.enabled)
    {
        result.setResult(ApiError_InvalidOperation, "云工程未开通");
        return result;
    }

    std::string projectName;
    std::string relativePath;
    std::string sha256;
    std::string err;
    if (!NormalizeProjectName(body.get("projectName", "").asString(), projectName, err) ||
        !NormalizeRelativePath(body.get("relativePath", "").asString(), relativePath, err) ||
        !NormalizeSha256(body.get("sha256", "").asString(), sha256, err))
    {
        result.setResult(ApiError_MissingRequiredArgs, err);
        return result;
    }
    long long size = body.get("size", 0).asInt64();

    try
    {
        TeamContext context;
        if (!LoadTeamContext(userId, context, err))
        {
            result.setResult(ApiError_NotTeamMember, err);
            return result;
        }
        if (!Writable(context))
        {
            result.setResult(ApiError_UserNotPaid, "订阅已到期，云工程只读");
            return result;
        }

        const std::string objectKey =
            config.keyPrefix + "/" + std::to_string(context.teamId) + "/" + projectName + "/" +
            relativePath;

        if (config.driver == "local")
        {
            std::string path;
            if (!LocalPathFor(objectKey, path) || !std::filesystem::exists(path))
            {
                result.setResult(ApiError_InvalidOperation, "上传文件不存在，请先完成上传");
                return result;
            }
            size = static_cast<long long>(std::filesystem::file_size(path));
        }
        else if (config.driver == "cos")
        {
            if (!CosConfigured(config))
            {
                result.setResult(ApiError_InvalidOperation, "云存储未配置");
                return result;
            }
            if (!CosObjectExists(config, objectKey, err))
            {
                result.setResult(ApiError_InvalidOperation, err);
                return result;
            }
        }
        else
        {
            result.setResult(ApiError_InvalidOperation, "未知的云存储驱动");
            return result;
        }

        if (size <= 0 || size > config.maxFileSizeBytes)
        {
            result.setResult(ApiError_InvalidOperation, "文件大小不合法");
            return result;
        }

        const long long existing = ExistingFileSize(context.teamId, objectKey);
        const long long used = UsedBytes(context.teamId);
        const long long quota = static_cast<long long>(QuotaGb(context)) * 1024 * 1024 * 1024;
        if (used - existing + size > quota)
        {
            result.setResult(ApiError_InvalidOperation,
                             "云工程空间不足：已用 " + Mb(used) + " MB / 配额 " +
                                 std::to_string(QuotaGb(context)) + " GB");
            return result;
        }

        auto db = drogon::app().getDbClient();
        auto rows = db->execSqlSync(
            "insert into cloud_project_file "
            "(team_id, uploaded_by, project_name, relative_path, object_key, file_size, sha256) "
            "values ($1, $2, $3, $4, $5, $6, $7) "
            "on conflict (team_id, object_key) do update set "
            "  uploaded_by = excluded.uploaded_by, project_name = excluded.project_name, "
            "  relative_path = excluded.relative_path, file_size = excluded.file_size, "
            "  sha256 = excluded.sha256, updated_at = now() "
            "returning id, project_name, relative_path, file_size, sha256, "
            "  to_char(updated_at at time zone 'Asia/Shanghai', "
            "          'YYYY-MM-DD\"T\"HH24:MI:SS') as updated_at",
            context.teamId, userId, projectName, relativePath, objectKey, size, sha256);

        result.jsondata = rows.empty() ? Json::Value(Json::objectValue) : FileJson(rows[0]);
        result.setResult(ApiError_Success);
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "云工程上传确认失败: " << e.base().what();
        result.setResult(ApiError_DatabaseError);
    }
    return result;
}

utils::HttpResult CloudProjectService::List(int userId, const std::string &projectName)
{
    utils::HttpResult result;
    const auto config = LoadConfig();
    if (!config.enabled)
    {
        result.jsondata["enabled"] = false;
        result.setResult(ApiError_Success);
        return result;
    }

    try
    {
        TeamContext context;
        std::string err;
        if (!LoadTeamContext(userId, context, err))
        {
            result.setResult(ApiError_NotTeamMember, err);
            return result;
        }

        auto db = drogon::app().getDbClient();
        std::string sql =
            "select id, project_name, relative_path, file_size, sha256, "
            "       to_char(updated_at at time zone 'Asia/Shanghai', "
            "               'YYYY-MM-DD\"T\"HH24:MI:SS') as updated_at "
            "from cloud_project_file where team_id = $1";
        if (!projectName.empty())
        {
            sql += " and project_name = $2";
        }
        sql += " order by project_name, relative_path";
        auto rows = projectName.empty() ? db->execSqlSync(sql, context.teamId)
                                        : db->execSqlSync(sql, context.teamId, projectName);

        Json::Value items(Json::arrayValue);
        long long total = 0;
        for (const auto &row : rows)
        {
            items.append(FileJson(row));
            total += row["file_size"].as<long long>();
        }
        result.jsondata["items"] = items;
        result.jsondata["count"] = static_cast<Json::Int64>(items.size());
        result.jsondata["totalBytes"] = static_cast<Json::Int64>(total);
        result.setResult(ApiError_Success);
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "云工程文件列表失败: " << e.base().what();
        result.setResult(ApiError_DatabaseError);
    }
    return result;
}

utils::HttpResult CloudProjectService::Download(int userId,
                                                long long fileId,
                                                const std::string &baseUrl)
{
    utils::HttpResult result;
    const auto config = LoadConfig();
    if (!config.enabled)
    {
        result.setResult(ApiError_InvalidOperation, "云工程未开通");
        return result;
    }

    try
    {
        TeamContext context;
        std::string err;
        if (!LoadTeamContext(userId, context, err))
        {
            result.setResult(ApiError_NotTeamMember, err);
            return result;
        }

        auto db = drogon::app().getDbClient();
        auto rows = db->execSqlSync(
            "select id, project_name, relative_path, object_key, file_size, sha256, "
            "       to_char(updated_at at time zone 'Asia/Shanghai', "
            "               'YYYY-MM-DD\"T\"HH24:MI:SS') as updated_at "
            "from cloud_project_file where id = $1 and team_id = $2",
            fileId, context.teamId);
        if (rows.empty())
        {
            result.setResult(ApiError_InvalidOperation, "文件不存在");
            return result;
        }

        const std::string objectKey = rows[0]["object_key"].as<std::string>();
        std::string downloadUrl;
        if (config.driver == "local")
        {
            const long long exp = NowUnix() + config.urlExpireSec;
            downloadUrl = baseUrl + "/api/cloud/direct?method=GET&key=" +
                          CosSigner::EncodeValue(objectKey) + "&exp=" + std::to_string(exp) +
                          "&token=" + LocalToken(config, objectKey, "GET", exp);
        }
        else
        {
            if (!CosConfigured(config))
            {
                result.setResult(ApiError_InvalidOperation, "云存储未配置");
                return result;
            }
            downloadUrl = CosSigner::PresignUrl("https", CosHost(config), objectKey,
                                                config.secretId, config.secretKey, "GET",
                                                NowUnix(), config.urlExpireSec);
        }

        result.jsondata["file"] = FileJson(rows[0]);
        result.jsondata["downloadUrl"] = downloadUrl;
        result.jsondata["expiresIn"] = config.urlExpireSec;
        result.setResult(ApiError_Success);
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "云工程下载地址生成失败: " << e.base().what();
        result.setResult(ApiError_DatabaseError);
    }
    return result;
}

utils::HttpResult CloudProjectService::Remove(int userId, long long fileId)
{
    utils::HttpResult result;
    const auto config = LoadConfig();
    if (!config.enabled)
    {
        result.setResult(ApiError_InvalidOperation, "云工程未开通");
        return result;
    }

    try
    {
        TeamContext context;
        std::string err;
        if (!LoadTeamContext(userId, context, err))
        {
            result.setResult(ApiError_NotTeamMember, err);
            return result;
        }

        auto db = drogon::app().getDbClient();
        auto rows = db->execSqlSync(
            "select id, object_key, uploaded_by from cloud_project_file "
            "where id = $1 and team_id = $2",
            fileId, context.teamId);
        if (rows.empty())
        {
            result.setResult(ApiError_InvalidOperation, "文件不存在");
            return result;
        }

        const int uploadedBy = rows[0]["uploaded_by"].isNull()
                                   ? 0
                                   : rows[0]["uploaded_by"].as<int>();
        if (uploadedBy != userId && !context.IsTeamAdmin())
        {
            result.setResult(ApiError_NotTeamAdmin, "只有上传者或团队管理员可以删除");
            return result;
        }

        const std::string objectKey = rows[0]["object_key"].as<std::string>();
        if (config.driver == "local")
        {
            std::string path;
            if (LocalPathFor(objectKey, path))
            {
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
        }
        else if (config.driver == "cos")
        {
            if (!CosConfigured(config))
            {
                result.setResult(ApiError_InvalidOperation, "云存储未配置");
                return result;
            }
            if (!CosDeleteObject(config, objectKey, err))
            {
                result.setResult(ApiError_InvalidOperation, err);
                return result;
            }
        }

        db->execSqlSync("delete from cloud_project_file where id = $1 and team_id = $2",
                        fileId, context.teamId);
        result.jsondata["deleted"] = true;
        result.setResult(ApiError_Success);
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "云工程删除失败: " << e.base().what();
        result.setResult(ApiError_DatabaseError);
    }
    return result;
}

} // namespace Services
} // namespace UEAdminAPI


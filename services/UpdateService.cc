#include "UpdateService.h"

#include <drogon/drogon.h>

#include "utils/ApiErrorCodes.h"

#include <openssl/sha.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

using namespace drogon;
using namespace UEAdminAPI;
using namespace UEAdminAPI::utils;

namespace UEAdminAPI {
namespace Services {

namespace {

// 把 "11.7.120" 解析成数值数组；非数字段按 0 处理
std::vector<int> ParseVersion(const std::string &version)
{
    std::vector<int> parts;
    std::string cur;
    for (char ch : version)
    {
        if (ch == '.')
        {
            parts.push_back(cur.empty() ? 0 : std::atoi(cur.c_str()));
            cur.clear();
        }
        else if (std::isdigit(static_cast<unsigned char>(ch)))
        {
            cur.push_back(ch);
        }
        else if (!cur.empty())
        {
            // 遇到非数字后缀（如 -beta）就结束
            break;
        }
    }
    if (!cur.empty())
    {
        parts.push_back(std::atoi(cur.c_str()));
    }
    while (parts.size() < 3)
    {
        parts.push_back(0);
    }
    return parts;
}

bool VersionGreater(const std::string &lhs, const std::string &rhs)
{
    return ParseVersion(lhs) > ParseVersion(rhs);
}

std::string Sha256Hex(std::string_view data)
{
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char *>(data.data()), data.size(), digest);

    static const char *kHex = "0123456789abcdef";
    std::string out;
    out.reserve(SHA256_DIGEST_LENGTH * 2);
    for (unsigned char byte : digest)
    {
        out.push_back(kHex[byte >> 4]);
        out.push_back(kHex[byte & 0x0F]);
    }
    return out;
}

std::string StoreDir()
{
    const auto &cfg = drogon::app().getCustomConfig();
    std::string dir = "./installers";
    if (cfg.isMember("Installer") && cfg["Installer"].isObject())
    {
        const auto &node = cfg["Installer"]["StoreDir"];
        if (node.isString() && !node.asString().empty())
        {
            dir = node.asString();
        }
    }
    return dir;
}

std::string ServerHost()
{
    const auto &cfg = drogon::app().getCustomConfig();
    return cfg.get("ServerHost", "").asString();
}

// 团队付费/授权/是否存在过付费记录
struct AccessState
{
    bool hasTeam = false;
    bool teamActive = false;
    bool paid = false;
    bool everSubscribed = false;
    bool updateAuth = false;
};

AccessState QueryAccess(const std::shared_ptr<drogon::orm::DbClient> &db, int userId)
{
    AccessState state;
    auto rows = db->execSqlSync(
        "select t.id as team_id, t.status as team_status, "
        "       (s.end_date is not null and s.end_date >= current_date) as paid, "
        "       (s.end_date is not null) as ever_subscribed, "
        "       coalesce(uf.enabled, tf.enabled, false) as update_auth "
        "from team_member tm "
        "join team t on t.id = tm.team_id "
        "left join lateral (select start_date, end_date from team_subscription ts "
        "                   where ts.team_id = t.id and ts.status = 1 "
        "                   order by ts.end_date desc limit 1) s on true "
        "left join team_feature_auth tf on tf.team_id = t.id and tf.feature_code = 'online_update' "
        "left join user_feature_auth uf on uf.user_id = tm.user_id and uf.feature_code = 'online_update' "
        "where tm.user_id = $1 and tm.status = 1",
        userId);
    if (rows.empty())
    {
        return state;
    }
    state.hasTeam = true;
    state.teamActive = !rows[0]["team_status"].isNull() && rows[0]["team_status"].as<int>() == 1;
    state.paid = !rows[0]["paid"].isNull() && rows[0]["paid"].as<bool>();
    state.everSubscribed = !rows[0]["ever_subscribed"].isNull() && rows[0]["ever_subscribed"].as<bool>();
    state.updateAuth = !rows[0]["update_auth"].isNull() && rows[0]["update_auth"].as<bool>();
    return state;
}

} // namespace

HttpResult UpdateService::CheckUpdate(int userId,
                                      const std::string &currentVersion,
                                      const std::string &platform)
{
    (void)platform; // 目前只有 Win32 一个平台
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        const AccessState access = QueryAccess(db, userId);

        auto latestRows = db->execSqlSync(
            "select id, version, build_no, file_name, size_bytes, sha256, notes, "
            "       to_char(published_at, 'YYYY-MM-DD HH24:MI:SS') as published_at "
            "from installer_version where status = 1 order by id desc limit 1");

        std::string latestVersion;
        if (!latestRows.empty())
        {
            latestVersion = latestRows[0]["version"].as<std::string>();
        }

        result.jsondata["currentVersion"] = currentVersion;
        result.jsondata["latestVersion"] = latestVersion;
        result.jsondata["hasUpdate"] = false;
        result.jsondata["reason"] = "no_update";

        if (!access.hasTeam)
        {
            result.jsondata["reason"] = "not_paid";
            result.setResult(ApiErrorCode::ApiError_Success, "not in a team");
            return result;
        }
        if (!access.teamActive)
        {
            result.jsondata["reason"] = "team_disabled";
            result.setResult(ApiErrorCode::ApiError_Success, "team disabled");
            return result;
        }
        if (!access.paid)
        {
            result.jsondata["reason"] = access.everSubscribed ? "expired" : "not_paid";
            result.setResult(ApiErrorCode::ApiError_Success, "team not paid");
            return result;
        }
        if (!access.updateAuth)
        {
            result.jsondata["reason"] = "no_auth";
            result.setResult(ApiErrorCode::ApiError_Success, "online_update not authorized");
            return result;
        }
        if (latestRows.empty())
        {
            result.jsondata["reason"] = "no_package";
            result.setResult(ApiErrorCode::ApiError_Success, "no installer released");
            return result;
        }
        if (!VersionGreater(latestVersion, currentVersion))
        {
            result.setResult(ApiErrorCode::ApiError_Success, "already latest");
            return result;
        }

        const auto &row = latestRows[0];
        result.jsondata["hasUpdate"] = true;
        result.jsondata["reason"] = "ok";
        result.jsondata["version"] = latestVersion;
        result.jsondata["buildNo"] = row["build_no"].isNull() ? 0 : row["build_no"].as<int>();
        result.jsondata["fileName"] = row["file_name"].as<std::string>();
        result.jsondata["sizeBytes"] =
            row["size_bytes"].isNull() ? 0 : static_cast<Json::Int64>(row["size_bytes"].as<int64_t>());
        result.jsondata["sha256"] = row["sha256"].as<std::string>();
        result.jsondata["notes"] = row["notes"].isNull() ? "" : row["notes"].as<std::string>();
        result.jsondata["publishedAt"] = row["published_at"].isNull() ? "" : row["published_at"].as<std::string>();
        result.jsondata["downloadUrl"] = ServerHost() + "/api/update/download/" + latestVersion;
        result.setResult(ApiErrorCode::ApiError_Success, "update available");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "CheckUpdate 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult UpdateService::AdminUploadInstaller(const std::string &version,
                                               const std::string &notes,
                                               int buildNo,
                                               const std::string &minVersion,
                                               const std::string &fileName,
                                               std::string_view content)
{
    HttpResult result;
    if (version.empty() || fileName.empty() || content.empty())
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "version / file 不能为空");
        return result;
    }
    // 只接受安装包后缀，避免把任意文件放到下载目录
    const std::string lower = [&fileName]() {
        std::string s = fileName;
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }();
    if (lower.size() < 4 || lower.compare(lower.size() - 4, 4, ".exe") != 0)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "只接受 .exe 安装包");
        return result;
    }
    // 文件名只取 basename，避免路径穿越
    std::string safeName = fileName;
    const auto slash = safeName.find_last_of("/\\");
    if (slash != std::string::npos)
    {
        safeName = safeName.substr(slash + 1);
    }

    auto db = drogon::app().getDbClient();
    try
    {
        auto existRows = db->execSqlSync(
            "select 1 from installer_version where version = $1", version);
        if (!existRows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_CodeAlreadyExists, "该版本号已存在");
            return result;
        }

        const std::filesystem::path dir = std::filesystem::path(StoreDir()) / version;
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec)
        {
            LOG_ERROR << "创建安装包目录失败: " << dir << " " << ec.message();
            result.setResult(ApiErrorCode::ApiError_InternalError, "创建存储目录失败");
            return result;
        }

        const std::filesystem::path target = dir / safeName;
        {
            std::ofstream ofs(target, std::ios::binary | std::ios::trunc);
            if (!ofs)
            {
                result.setResult(ApiErrorCode::ApiError_InternalError, "写入安装包失败");
                return result;
            }
            ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
        }

        const std::string sha = Sha256Hex(content);
        auto rows = db->execSqlSync(
            "insert into installer_version (version, build_no, file_name, file_path, size_bytes, sha256, notes, min_version, status) "
            "values ($1, $2::int, $3, $4, $5::bigint, $6, $7, $8, 0) "
            "returning id",
            version, buildNo, safeName, target.string(),
            static_cast<int64_t>(content.size()), sha, notes, minVersion);
        if (rows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_DatabaseError);
            return result;
        }

        result.jsondata["id"] = rows[0]["id"].as<int>();
        result.jsondata["version"] = version;
        result.jsondata["fileName"] = safeName;
        result.jsondata["sizeBytes"] = static_cast<Json::Int64>(content.size());
        result.jsondata["sha256"] = sha;
        result.jsondata["minVersion"] = minVersion;
        result.jsondata["status"] = 0;
        result.setResult(ApiErrorCode::ApiError_Success, "上传成功，待发布");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "AdminUploadInstaller 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult UpdateService::AdminPublishInstaller(int installerId)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync(
            "select id, version from installer_version where id = $1", installerId);
        if (rows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_InstallerNotFound);
            return result;
        }

        // 同一时刻只保留一个已发布版本，其余置为下线（记录仍保留）
        db->execSqlSync(
            "with unpublish as (update installer_version set status = 2 where status = 1) "
            "update installer_version set status = 1, published_at = now() where id = $1",
            installerId);

        result.jsondata["id"] = installerId;
        result.jsondata["version"] = rows[0]["version"].as<std::string>();
        result.jsondata["status"] = 1;
        result.setResult(ApiErrorCode::ApiError_Success, "已发布");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "AdminPublishInstaller 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult UpdateService::AdminUpdateInstaller(int installerId,
                                               const std::string &version,
                                               int buildNo,
                                               const std::string &notes,
                                               const std::string &minVersion)
{
    HttpResult result;
    if (version.empty())
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "版本号不能为空");
        return result;
    }
    if (version.size() > 64 || minVersion.size() > 64)
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "版本号/最低版本长度不能超过 64");
        return result;
    }
    if (buildNo < 0)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "构建号不能为负数");
        return result;
    }

    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync(
            "select id, version from installer_version where id = $1", installerId);
        if (rows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_InstallerNotFound);
            return result;
        }

        if (version != rows[0]["version"].as<std::string>())
        {
            auto dupRows = db->execSqlSync(
                "select 1 from installer_version where version = $1 and id <> $2",
                version, installerId);
            if (!dupRows.empty())
            {
                result.setResult(ApiErrorCode::ApiError_CodeAlreadyExists, "该版本号已存在");
                return result;
            }
        }

        // 只改元数据：磁盘文件与 file_path 保持不动，下载时仍按这条记录取文件
        db->execSqlSync(
            "update installer_version set version = $2, build_no = $3::int, "
            "       notes = $4, min_version = $5 where id = $1",
            installerId, version, buildNo, notes, minVersion);

        result.jsondata["id"] = installerId;
        result.jsondata["version"] = version;
        result.jsondata["buildNo"] = buildNo;
        result.jsondata["notes"] = notes;
        result.jsondata["minVersion"] = minVersion;
        result.setResult(ApiErrorCode::ApiError_Success, "安装包信息已更新");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "AdminUpdateInstaller 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult UpdateService::AdminOfflineInstaller(int installerId)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync(
            "select id, version, status from installer_version where id = $1", installerId);
        if (rows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_InstallerNotFound);
            return result;
        }

        db->execSqlSync(
            "update installer_version set status = 2 where id = $1", installerId);

        result.jsondata["id"] = installerId;
        result.jsondata["version"] = rows[0]["version"].as<std::string>();
        result.jsondata["status"] = 2;
        result.setResult(ApiErrorCode::ApiError_Success, "已下线");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "AdminOfflineInstaller 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult UpdateService::AdminListInstallers()
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync(
            "select id, version, build_no, file_name, size_bytes, sha256, notes, min_version, status, "
            "       to_char(created_at, 'YYYY-MM-DD HH24:MI:SS') as created_at, "
            "       to_char(published_at, 'YYYY-MM-DD HH24:MI:SS') as published_at "
            "from installer_version order by id desc");

        Json::Value items(Json::arrayValue);
        for (const auto &r : rows)
        {
            Json::Value item;
            item["id"] = r["id"].as<int>();
            item["version"] = r["version"].as<std::string>();
            item["buildNo"] = r["build_no"].isNull() ? 0 : r["build_no"].as<int>();
            item["fileName"] = r["file_name"].as<std::string>();
            item["sizeBytes"] =
                r["size_bytes"].isNull() ? 0 : static_cast<Json::Int64>(r["size_bytes"].as<int64_t>());
            item["sha256"] = r["sha256"].as<std::string>();
            item["notes"] = r["notes"].isNull() ? "" : r["notes"].as<std::string>();
            item["minVersion"] = r["min_version"].isNull() ? "" : r["min_version"].as<std::string>();
            item["status"] = r["status"].as<int>();
            item["createdAt"] = r["created_at"].isNull() ? "" : r["created_at"].as<std::string>();
            item["publishedAt"] = r["published_at"].isNull() ? "" : r["published_at"].as<std::string>();
            items.append(item);
        }
        result.jsondata["installers"] = items;
        result.setResult(ApiErrorCode::ApiError_Success, "success");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "AdminListInstallers 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

bool UpdateService::ResolveDownload(int userId, const std::string &version,
                                    std::string &outPath, std::string &outFileName,
                                    HttpResult &errorResult)
{
    auto db = drogon::app().getDbClient();
    try
    {
        const AccessState access = QueryAccess(db, userId);
        if (!access.hasTeam || !access.paid)
        {
            errorResult.setResult(ApiErrorCode::ApiError_UserNotPaid);
            return false;
        }
        if (!access.updateAuth)
        {
            errorResult.setResult(ApiErrorCode::ApiError_FeatureNotAuthorized);
            return false;
        }

        auto rows = db->execSqlSync(
            "select file_path, file_name from installer_version "
            "where version = $1 and status = 1 order by id desc limit 1",
            version);
        if (rows.empty())
        {
            errorResult.setResult(ApiErrorCode::ApiError_InstallerNotFound);
            return false;
        }

        outPath = rows[0]["file_path"].as<std::string>();
        outFileName = rows[0]["file_name"].as<std::string>();

        std::error_code ec;
        if (!std::filesystem::exists(outPath, ec))
        {
            LOG_ERROR << "安装包文件缺失: " << outPath;
            errorResult.setResult(ApiErrorCode::ApiError_InternalError, "安装包文件缺失");
            return false;
        }
        return true;
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "ResolveDownload 失败: " << e.base().what();
        errorResult.setResult(ApiErrorCode::ApiError_DatabaseError);
        return false;
    }
}

} // namespace Services
} // namespace UEAdminAPI

#include "UpdateService.h"

#include <drogon/drogon.h>

#include "utils/ApiErrorCodes.h"

#include <openssl/sha.h>
#include <openssl/evp.h>

#include <drogon/utils/Utilities.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <mutex>
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

// 分片写入/完成落盘串行化：避免同一会话被并发请求写乱，也防止完成时还有分片在写
std::mutex g_uploadFileMutex;

constexpr int64_t kUploadChunkSize = 4 * 1024 * 1024;
constexpr size_t kMaxChunkBytes = 8 * 1024 * 1024;
constexpr int64_t kMaxUploadFileSize = 8LL * 1024 * 1024 * 1024;

std::string UploadTempDir()
{
    const auto &cfg = drogon::app().getCustomConfig();
    std::string dir = "./uploads/installer-tmp";
    if (cfg.isMember("Installer") && cfg["Installer"].isObject())
    {
        const auto &node = cfg["Installer"]["UploadTempDir"];
        if (node.isString() && !node.asString().empty())
        {
            dir = node.asString();
        }
    }
    return dir;
}

bool IsSha256Hex(const std::string &text)
{
    if (text.size() != 64)
    {
        return false;
    }
    for (char ch : text)
    {
        if (!std::isxdigit(static_cast<unsigned char>(ch)))
        {
            return false;
        }
    }
    return true;
}

bool HexEqualIgnoreCase(const std::string &lhs, const std::string &rhs)
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }
    for (size_t i = 0; i < lhs.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(lhs[i])) !=
            std::tolower(static_cast<unsigned char>(rhs[i])))
        {
            return false;
        }
    }
    return true;
}

// 流式计算文件 sha256，避免把安装包整体读进内存
bool Sha256FileHex(const std::filesystem::path &path, std::string &outHex)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return false;
    }

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx == nullptr)
    {
        return false;
    }
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1)
    {
        EVP_MD_CTX_free(ctx);
        return false;
    }

    std::vector<char> buffer(1024 * 1024);
    while (in)
    {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize readBytes = in.gcount();
        if (readBytes > 0 && EVP_DigestUpdate(ctx, buffer.data(), static_cast<size_t>(readBytes)) != 1)
        {
            EVP_MD_CTX_free(ctx);
            return false;
        }
    }

    unsigned char digest[EVP_MAX_MD_SIZE] = {0};
    unsigned int digestLen = 0;
    const bool ok = EVP_DigestFinal_ex(ctx, digest, &digestLen) == 1;
    EVP_MD_CTX_free(ctx);
    if (!ok)
    {
        return false;
    }

    static const char *kHex = "0123456789abcdef";
    outHex.clear();
    outHex.reserve(digestLen * 2);
    for (unsigned int i = 0; i < digestLen; ++i)
    {
        outHex.push_back(kHex[digest[i] >> 4]);
        outHex.push_back(kHex[digest[i] & 0x0F]);
    }
    return true;
}

// 清理 7 天没有更新的会话与临时文件（含已完成会话）
void CleanupStaleUploadSessions(const std::shared_ptr<drogon::orm::DbClient> &db)
{
    try
    {
        auto rows = db->execSqlSync(
            "select id, coalesce(temp_name, '') as temp_name "
            "from installer_upload_session where updated_at < now() - interval '7 days'");
        for (const auto &row : rows)
        {
            const std::string tempName = row["temp_name"].as<std::string>();
            if (!tempName.empty())
            {
                std::error_code ec;
                std::filesystem::remove(
                    std::filesystem::path(UploadTempDir()) / std::filesystem::path(tempName).filename(), ec);
            }
            db->execSqlSync("delete from installer_upload_session where id = $1",
                            row["id"].as<std::string>());
        }
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_WARN << "CleanupStaleUploadSessions 失败: " << e.base().what();
    }
}

bool IsSafeVersion(const std::string &version)
{
    if (version.empty() || version.size() > 64 || version.find("..") != std::string::npos)
    {
        return false;
    }
    for (char ch : version)
    {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '.' || ch == '_' || ch == '-'))
        {
            return false;
        }
    }
    return true;
}

bool IsExeFileName(const std::string &fileName)
{
    std::string lower = fileName;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".exe") == 0;
}

std::string BaseName(const std::string &fileName)
{
    std::string name = fileName;
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos)
    {
        name = name.substr(slash + 1);
    }
    return name;
}

int64_t FileSizeOrZero(const std::filesystem::path &path)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec)
    {
        return 0;
    }
    return static_cast<int64_t>(size);
}

void FillInstallerJson(Json::Value &data, int id, const std::string &version, int64_t buildNo,
                       const std::string &fileName, int64_t sizeBytes, const std::string &sha256,
                       const std::string &minVersion, int status)
{
    data["id"] = id;
    data["version"] = version;
    data["buildNo"] = static_cast<Json::Int64>(buildNo);
    data["fileName"] = fileName;
    data["sizeBytes"] = static_cast<Json::Int64>(sizeBytes);
    data["sha256"] = sha256;
    data["minVersion"] = minVersion;
    data["status"] = status;
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
            "       to_char(published_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD HH24:MI:SS') as published_at "
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
        result.jsondata["buildNo"] = static_cast<Json::Int64>(row["build_no"].isNull() ? 0 : row["build_no"].as<int64_t>());
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

HttpResult UpdateService::AdminInitUpload(int userId,
                                          const std::string &version,
                                          int64_t buildNo,
                                          const std::string &notes,
                                          const std::string &minVersion,
                                          const std::string &fileName,
                                          int64_t fileSize,
                                          const std::string &fileSha256)
{
    HttpResult result;
    if (version.empty() || fileName.empty())
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "version / fileName 不能为空");
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
    if (fileSize <= 0 || fileSize > kMaxUploadFileSize)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "安装包大小不合法");
        return result;
    }
    if (!IsSafeVersion(version))
    {
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "版本号包含非法字符");
        return result;
    }
    if (notes.size() > 2000 || BaseName(fileName).size() > 255)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "备注或文件名过长");
        return result;
    }
    if (!IsExeFileName(fileName))
    {
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "只接受 .exe 安装包");
        return result;
    }
    if (!IsSha256Hex(fileSha256))
    {
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "缺少有效的文件 sha256");
        return result;
    }
    const std::string safeName = BaseName(fileName);
    std::string sha = fileSha256;
    std::transform(sha.begin(), sha.end(), sha.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    auto db = drogon::app().getDbClient();
    try
    {
        // 同一版本已经传过：sha 相同直接当成功，避免客户端重传
        auto versionRows = db->execSqlSync(
            "select id, version, build_no, file_name, size_bytes, coalesce(sha256, '') as sha256, "
            "       coalesce(min_version, '') as min_version, status "
            "from installer_version where version = $1",
            version);
        if (!versionRows.empty())
        {
            const auto &row = versionRows[0];
            const std::string existingSha = row["sha256"].as<std::string>();
            if (HexEqualIgnoreCase(existingSha, sha))
            {
                result.jsondata["alreadyUploaded"] = true;
                FillInstallerJson(result.jsondata,
                                  row["id"].as<int>(),
                                  row["version"].as<std::string>(),
                                  row["build_no"].isNull() ? 0 : row["build_no"].as<int64_t>(),
                                  row["file_name"].as<std::string>(),
                                  row["size_bytes"].isNull() ? 0 : row["size_bytes"].as<int64_t>(),
                                  existingSha,
                                  row["min_version"].as<std::string>(),
                                  row["status"].as<int>());
                result.setResult(ApiErrorCode::ApiError_Success, "该版本已上传");
                return result;
            }
            result.setResult(ApiErrorCode::ApiError_CodeAlreadyExists, "该版本号已存在");
            return result;
        }

        // 同一个管理员、同一个文件（sha256+大小）找回未完成会话
        auto sessionRows = db->execSqlSync(
            "select id, temp_name, uploaded_bytes from installer_upload_session "
            "where user_id = $1 and file_sha256 = $2 and file_size = $3 and status = 0 "
            "order by updated_at desc limit 1",
            userId, sha, fileSize);
        if (!sessionRows.empty())
        {
            const std::string uploadId = sessionRows[0]["id"].as<std::string>();
            const std::string tempName = sessionRows[0]["temp_name"].as<std::string>();
            const auto tempPath = std::filesystem::path(UploadTempDir()) /
                                  std::filesystem::path(tempName).filename();

            int64_t actual = FileSizeOrZero(tempPath);
            if (actual < 0 || actual > fileSize)
            {
                std::error_code ec;
                std::filesystem::remove(tempPath, ec);
                actual = 0;
            }

            db->execSqlSync(
                "update installer_upload_session "
                "set version = $2, build_no = $3, notes = $4, min_version = $5, "
                "    uploaded_bytes = $6, file_name = $7, updated_at = now() where id = $1",
                uploadId, version, buildNo, notes, minVersion, actual, safeName);

            result.jsondata["uploadId"] = uploadId;
            result.jsondata["offset"] = static_cast<Json::Int64>(actual);
            result.jsondata["chunkSize"] = static_cast<Json::Int64>(kUploadChunkSize);
            result.jsondata["resumed"] = actual > 0;
            result.setResult(ApiErrorCode::ApiError_Success, "resume upload");
            return result;
        }

        CleanupStaleUploadSessions(db);

        const std::string uploadId = drogon::utils::getUuid();
        const std::string tempName = uploadId + ".part";
        const std::filesystem::path tempDir = UploadTempDir();
        const std::filesystem::path tempPath = tempDir / tempName;

        std::error_code ec;
        std::filesystem::create_directories(tempDir, ec);
        if (ec)
        {
            LOG_ERROR << "创建安装包临时目录失败: " << tempDir << " " << ec.message();
            result.setResult(ApiErrorCode::ApiError_InternalError, "创建临时目录失败");
            return result;
        }
        {
            std::ofstream ofs(tempPath, std::ios::binary | std::ios::trunc);
            if (!ofs)
            {
                result.setResult(ApiErrorCode::ApiError_InternalError, "创建临时文件失败");
                return result;
            }
        }

        try
        {
            db->execSqlSync(
                "insert into installer_upload_session "
                "(id, user_id, version, build_no, notes, min_version, file_name, file_size, "
                " file_sha256, temp_name, uploaded_bytes, status) "
                "values ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, 0, 0)",
                uploadId, userId, version, buildNo, notes, minVersion, safeName, fileSize,
                sha, tempName);
        }
        catch (...)
        {
            std::error_code removeEc;
            std::filesystem::remove(tempPath, removeEc);
            throw;
        }

        result.jsondata["uploadId"] = uploadId;
        result.jsondata["offset"] = static_cast<Json::Int64>(0);
        result.jsondata["chunkSize"] = static_cast<Json::Int64>(kUploadChunkSize);
        result.jsondata["resumed"] = false;
        result.setResult(ApiErrorCode::ApiError_Success, "upload session created");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "AdminInitUpload 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult UpdateService::AdminUploadChunk(int userId,
                                           const std::string &uploadId,
                                           int64_t offset,
                                           std::string_view data)
{
    HttpResult result;
    if (uploadId.empty() || uploadId.size() > 64 || offset < 0)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "上传会话参数不合法");
        return result;
    }
    if (data.size() > kMaxChunkBytes)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "分片过大");
        return result;
    }

    auto db = drogon::app().getDbClient();
    try
    {
        std::lock_guard<std::mutex> fileLock(g_uploadFileMutex);
        auto rows = db->execSqlSync(
            "select temp_name, file_size, status from installer_upload_session "
            "where id = $1 and user_id = $2",
            uploadId, userId);
        if (rows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_InvalidOperation, "上传会话不存在");
            return result;
        }
        if (rows[0]["status"].as<int>() != 0)
        {
            result.setResult(ApiErrorCode::ApiError_InvalidOperation, "上传会话已完成");
            return result;
        }

        const int64_t fileSize = rows[0]["file_size"].as<int64_t>();
        const std::string tempName = rows[0]["temp_name"].as<std::string>();
        const auto tempPath = std::filesystem::path(UploadTempDir()) /
                              std::filesystem::path(tempName).filename();
        const int64_t actual = FileSizeOrZero(tempPath);

        // 上次响应丢失时客户端会拿旧 offset 重发：直接返回真实偏移让客户端重同步
        if (offset != actual)
        {
            result.jsondata["offset"] = static_cast<Json::Int64>(actual);
            result.setResult(ApiErrorCode::ApiError_Success, "offset resync");
            return result;
        }
        if (actual + static_cast<int64_t>(data.size()) > fileSize)
        {
            result.setResult(ApiErrorCode::ApiError_InvalidOperation, "分片超出安装包大小");
            return result;
        }
        if (!data.empty())
        {
            std::ofstream ofs(tempPath, std::ios::binary | std::ios::app);
            if (!ofs)
            {
                result.setResult(ApiErrorCode::ApiError_InternalError, "写入临时文件失败");
                return result;
            }
            ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
            ofs.flush();
            if (!ofs)
            {
                result.setResult(ApiErrorCode::ApiError_InternalError, "写入临时文件失败");
                return result;
            }
        }

        const int64_t newOffset = actual + static_cast<int64_t>(data.size());
        db->execSqlSync(
            "update installer_upload_session set uploaded_bytes = $2, updated_at = now() "
            "where id = $1",
            uploadId, newOffset);

        result.jsondata["offset"] = static_cast<Json::Int64>(newOffset);
        result.setResult(ApiErrorCode::ApiError_Success, "chunk stored");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "AdminUploadChunk 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult UpdateService::AdminCompleteUpload(int userId, const std::string &uploadId)
{
    HttpResult result;
    if (uploadId.empty() || uploadId.size() > 64)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "上传会话参数不合法");
        return result;
    }

    auto db = drogon::app().getDbClient();
    try
    {
        std::lock_guard<std::mutex> fileLock(g_uploadFileMutex);
        auto rows = db->execSqlSync(
            "select version, build_no, notes, min_version, file_name, file_size, file_sha256, "
            "       temp_name, status, installer_id "
            "from installer_upload_session where id = $1 and user_id = $2",
            uploadId, userId);
        if (rows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_InvalidOperation, "上传会话不存在");
            return result;
        }

        const auto &session = rows[0];
        const int status = session["status"].as<int>();
        const int installerId = session["installer_id"].isNull() ? 0 : session["installer_id"].as<int>();

        // 重复 complete（上次响应丢失）直接返回已有安装包
        if (status == 1 && installerId > 0)
        {
            auto instRows = db->execSqlSync(
                "select id, version, build_no, file_name, size_bytes, coalesce(sha256, '') as sha256, "
                "       coalesce(min_version, '') as min_version, status "
                "from installer_version where id = $1",
                installerId);
            if (instRows.empty())
            {
                result.setResult(ApiErrorCode::ApiError_InstallerNotFound);
                return result;
            }
            const auto &inst = instRows[0];
            FillInstallerJson(result.jsondata,
                              inst["id"].as<int>(),
                              inst["version"].as<std::string>(),
                              inst["build_no"].isNull() ? 0 : inst["build_no"].as<int64_t>(),
                              inst["file_name"].as<std::string>(),
                              inst["size_bytes"].isNull() ? 0 : inst["size_bytes"].as<int64_t>(),
                              inst["sha256"].as<std::string>(),
                              inst["min_version"].as<std::string>(),
                              inst["status"].as<int>());
            result.setResult(ApiErrorCode::ApiError_Success, "上传已完成");
            return result;
        }

        const std::string version = session["version"].as<std::string>();
        const std::string safeName = session["file_name"].as<std::string>();
        if (!IsSafeVersion(version) || safeName.empty())
        {
            result.setResult(ApiErrorCode::ApiError_InvalidOperation, "上传会话中的版本号或文件名不合法");
            return result;
        }
        const int64_t fileSize = session["file_size"].as<int64_t>();
        const std::string expectedSha = session["file_sha256"].as<std::string>();
        const std::string tempName = session["temp_name"].as<std::string>();
        const auto tempPath = std::filesystem::path(UploadTempDir()) /
                              std::filesystem::path(tempName).filename();

        const int64_t actual = FileSizeOrZero(tempPath);
        if (actual != fileSize)
        {
            result.jsondata["offset"] = static_cast<Json::Int64>(actual);
            result.setResult(ApiErrorCode::ApiError_InvalidOperation,
                             "安装包尚未上传完整，请继续续传");
            return result;
        }

        std::string actualSha;
        if (!Sha256FileHex(tempPath, actualSha))
        {
            result.setResult(ApiErrorCode::ApiError_InternalError, "计算文件 sha256 失败");
            return result;
        }
        if (!HexEqualIgnoreCase(actualSha, expectedSha))
        {
            std::error_code ec;
            std::filesystem::remove(tempPath, ec);
            db->execSqlSync("delete from installer_upload_session where id = $1", uploadId);
            result.setResult(ApiErrorCode::ApiError_InvalidOperation,
                             "文件校验失败（sha256 不一致），已清除临时文件，请重新上传");
            return result;
        }

        // 版本号可能在上传期间被另一个请求占用
        auto dupRows = db->execSqlSync(
            "select id, version, build_no, file_name, size_bytes, coalesce(sha256, '') as sha256, "
            "       coalesce(min_version, '') as min_version, status "
            "from installer_version where version = $1",
            version);
        if (!dupRows.empty())
        {
            const auto &dup = dupRows[0];
            if (HexEqualIgnoreCase(dup["sha256"].as<std::string>(), actualSha))
            {
                std::error_code ec;
                std::filesystem::remove(tempPath, ec);
                db->execSqlSync(
                    "update installer_upload_session set status = 1, installer_id = $2, "
                    "temp_name = '', uploaded_bytes = file_size, updated_at = now() where id = $1",
                    uploadId, dup["id"].as<int>());
                FillInstallerJson(result.jsondata,
                                  dup["id"].as<int>(),
                                  dup["version"].as<std::string>(),
                                  dup["build_no"].isNull() ? 0 : dup["build_no"].as<int64_t>(),
                                  dup["file_name"].as<std::string>(),
                                  dup["size_bytes"].isNull() ? 0 : dup["size_bytes"].as<int64_t>(),
                                  dup["sha256"].as<std::string>(),
                                  dup["min_version"].as<std::string>(),
                                  dup["status"].as<int>());
                result.setResult(ApiErrorCode::ApiError_Success, "该版本已上传");
                return result;
            }
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
        std::filesystem::remove(target, ec);
        std::filesystem::rename(tempPath, target, ec);
        if (ec)
        {
            std::error_code copyEc;
            std::filesystem::copy_file(tempPath, target,
                                       std::filesystem::copy_options::overwrite_existing, copyEc);
            if (copyEc)
            {
                LOG_ERROR << "移动安装包失败: " << tempPath << " -> " << target
                          << " " << copyEc.message();
                result.setResult(ApiErrorCode::ApiError_InternalError, "保存安装包失败");
                return result;
            }
            std::error_code removeEc;
            std::filesystem::remove(tempPath, removeEc);
        }

        const int64_t buildNo = session["build_no"].isNull() ? 0 : session["build_no"].as<int64_t>();
        const std::string notes = session["notes"].isNull() ? "" : session["notes"].as<std::string>();
        const std::string minVersion = session["min_version"].isNull()
            ? "" : session["min_version"].as<std::string>();

        int newId = 0;
        try
        {
            auto insertRows = db->execSqlSync(
                "insert into installer_version "
                "(version, build_no, file_name, file_path, size_bytes, sha256, notes, min_version, status) "
                "values ($1, $2::bigint, $3, $4, $5::bigint, $6, $7, $8, 0) returning id",
                version, buildNo, safeName, target.string(), fileSize, actualSha, notes, minVersion);
            if (insertRows.empty())
            {
                result.setResult(ApiErrorCode::ApiError_DatabaseError);
                std::filesystem::remove(target, ec);
                return result;
            }
            newId = insertRows[0]["id"].as<int>();
        }
        catch (...)
        {
            std::filesystem::remove(target, ec);
            throw;
        }

        db->execSqlSync(
            "update installer_upload_session set status = 1, installer_id = $2, "
            "temp_name = '', uploaded_bytes = file_size, updated_at = now() where id = $1",
            uploadId, newId);

        FillInstallerJson(result.jsondata, newId, version, buildNo, safeName, fileSize,
                          actualSha, minVersion, 0);
        result.setResult(ApiErrorCode::ApiError_Success, "上传完成，待发布");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "AdminCompleteUpload 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult UpdateService::AdminUploadInstaller(const std::string &version,
                                               const std::string &notes,
                                               int64_t buildNo,
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
            "values ($1, $2::bigint, $3, $4, $5::bigint, $6, $7, $8, 0) "
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
        result.jsondata["buildNo"] = static_cast<Json::Int64>(buildNo);
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
                                               int64_t buildNo,
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
            "update installer_version set version = $2, build_no = $3::bigint, "
            "       notes = $4, min_version = $5 where id = $1",
            installerId, version, buildNo, notes, minVersion);

        result.jsondata["id"] = installerId;
        result.jsondata["version"] = version;
        result.jsondata["buildNo"] = static_cast<Json::Int64>(buildNo);
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
            "       to_char(created_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD HH24:MI:SS') as created_at, "
            "       to_char(published_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD HH24:MI:SS') as published_at "
            "from installer_version order by id desc");

        Json::Value items(Json::arrayValue);
        for (const auto &r : rows)
        {
            Json::Value item;
            item["id"] = r["id"].as<int>();
            item["version"] = r["version"].as<std::string>();
            item["buildNo"] = static_cast<Json::Int64>(r["build_no"].isNull() ? 0 : r["build_no"].as<int64_t>());
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

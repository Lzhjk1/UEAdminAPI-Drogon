#include "MarketService.h"

#include <drogon/drogon.h>

#include "utils/ApiErrorCodes.h"
#include "utils/EnumUserPrivileges.h"

#include <openssl/evp.h>
#include <drogon/utils/Utilities.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

using namespace drogon;
using namespace UEAdminAPI;
using namespace UEAdminAPI::utils;

namespace UEAdminAPI {
namespace Services {
namespace {

constexpr int64_t kChunkSize = 4 * 1024 * 1024;
constexpr size_t kMaxChunkBytes = 8 * 1024 * 1024;
constexpr int64_t kMaxFileSize = 200LL * 1024 * 1024;
constexpr size_t kMaxNameLength = 80;
constexpr size_t kMaxVersionLength = 32;
constexpr size_t kMaxDescLength = 1000;

std::mutex g_marketFileMutex;

std::string MarketDir(const char *key, const char *fallback)
{
    const auto &cfg = drogon::app().getCustomConfig();
    std::string dir = fallback;
    if (cfg.isMember("Market") && cfg["Market"].isObject())
    {
        const auto &node = cfg["Market"][key];
        if (node.isString() && !node.asString().empty())
        {
            dir = node.asString();
        }
    }
    return dir;
}

std::string StoreDir()
{
    return MarketDir("StoreDir", "./market");
}

std::string UploadTempDir()
{
    return MarketDir("UploadTempDir", "./uploads/market-tmp");
}

std::string Trim(const std::string &value)
{
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
    {
        return std::string();
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

bool IsAllowedCategory(const std::string &category)
{
    return category == "component" || category == "standard";
}

bool IsAllowedExtension(const std::string &name)
{
    const auto dot = name.find_last_of('.');
    if (dot == std::string::npos)
    {
        return false;
    }
    std::string ext = name.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext == ".mdb" || ext == ".db" || ext == ".zip";
}

std::string SafeFileName(const std::string &raw)
{
    std::string name = raw;
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos)
    {
        name = name.substr(slash + 1);
    }
    std::string out;
    out.reserve(name.size());
    for (unsigned char ch : name)
    {
        if (std::isalnum(ch) || ch == '.' || ch == '_' || ch == '-' || ch == ' ' || ch >= 0x80)
        {
            out.push_back(static_cast<char>(ch));
        }
        else
        {
            out.push_back('_');
        }
    }
    while (!out.empty() && (out.front() == '.' || out.front() == ' '))
    {
        out.erase(out.begin());
    }
    while (!out.empty() && out.back() == ' ')
    {
        out.pop_back();
    }
    if (out.size() > 120)
    {
        out = out.substr(out.size() - 120);
    }
    return out;
}

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

int64_t FileSizeOrZero(const std::filesystem::path &path)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec)
    {
        return -1;
    }
    return static_cast<int64_t>(size);
}

bool IsAdminUser(const std::shared_ptr<drogon::orm::DbClient> &db, int userId)
{
    try
    {
        auto rows = db->execSqlSync(
            "select (coalesce(u.privilege, 0) & $2::int) <> 0 as bit_admin, "
            "       exists(select 1 from user_system_role usr "
            "              join role r on r.id = usr.role_id "
            "              where usr.user_id = u.id and r.code = 'super_admin') as role_admin "
            "from \"user\" u where u.id = $1",
            userId, static_cast<int>(UserPrivileges::Admin));
        if (rows.empty())
        {
            return false;
        }
        return rows[0]["bit_admin"].as<bool>() || rows[0]["role_admin"].as<bool>();
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "MarketService 查询管理员失败: " << e.base().what();
        return false;
    }
}

void FillItem(Json::Value &out, const drogon::orm::Row &row)
{
    out["id"] = static_cast<Json::Int64>(row["id"].as<int64_t>());
    out["name"] = row["name"].as<std::string>();
    out["version"] = row["version"].as<std::string>();
    out["description"] = row["description"].as<std::string>();
    out["category"] = row["category"].as<std::string>();
    out["fileName"] = row["file_name"].as<std::string>();
    out["fileSize"] = static_cast<Json::Int64>(row["file_size"].as<int64_t>());
    out["downloadCount"] = static_cast<Json::Int64>(row["download_count"].as<int64_t>());
    out["createdDate"] = row["created_date"].as<std::string>();
    out["ownerName"] = row["owner_name"].as<std::string>();
    out["mine"] = row["mine"].as<bool>();
}

const char *kItemSelect =
    "select r.id, r.name, r.version, r.description, r.category, r.file_name, r.file_size, "
    "       r.download_count, coalesce(u.name, '') as owner_name, "
    "       to_char(r.created_at, 'YYYY-MM-DD') as created_date, "
    "       (r.owner_user_id = $1::int) as mine "
    "from market_resource r left join \"user\" u on u.id = r.owner_user_id ";

const char *kItemFilter =
    "where r.status = 1 "
    "  and ($2::text = '' or r.category = $2) "
    "  and ($3::text = '' or r.name ilike '%' || $3 || '%' "
    "       or r.description ilike '%' || $3 || '%' "
    "       or coalesce(u.name, '') ilike '%' || $3 || '%') ";

void CleanupStaleSessions(const std::shared_ptr<drogon::orm::DbClient> &db, int userId)
{
    try
    {
        auto rows = db->execSqlSync(
            "select id, temp_name from market_upload_session "
            "where user_id = $1 and status = 0 and updated_at < now() - interval '24 hours'",
            userId);
        for (const auto &row : rows)
        {
            std::error_code ec;
            std::filesystem::remove(
                std::filesystem::path(UploadTempDir()) /
                    std::filesystem::path(row["temp_name"].as<std::string>()).filename(),
                ec);
            db->execSqlSync("delete from market_upload_session where id = $1",
                            row["id"].as<std::string>());
        }
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_WARN << "MarketService 清理过期上传会话失败: " << e.base().what();
    }
}

} // namespace

HttpResult MarketService::ListResources(int userId,
                                        const std::string &category,
                                        const std::string &keyword,
                                        int page,
                                        int pageSize)
{
    HttpResult result;
    const std::string cat = Trim(category);
    const std::string kw = Trim(keyword);
    if (!cat.empty() && !IsAllowedCategory(cat))
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "category 不合法");
        return result;
    }
    if (kw.size() > 60)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "搜索词过长");
        return result;
    }
    if (page < 1)
    {
        page = 1;
    }
    if (pageSize < 1 || pageSize > 200)
    {
        pageSize = 200;
    }

    auto db = drogon::app().getDbClient();
    try
    {
        const auto rows = db->execSqlSync(
            std::string(kItemSelect) + kItemFilter +
                "order by r.id desc limit $4::int offset $5::int",
            userId, cat, kw, pageSize, (page - 1) * pageSize);
        Json::Value items(Json::arrayValue);
        for (const auto &row : rows)
        {
            Json::Value item;
            FillItem(item, row);
            items.append(item);
        }

        const auto totalRows = db->execSqlSync(
            "select count(*)::bigint as total from market_resource r "
            "left join \"user\" u on u.id = r.owner_user_id "
            "where r.status = 1 "
            "  and ($1::text = '' or r.category = $1) "
            "  and ($2::text = '' or r.name ilike '%' || $2 || '%' "
            "       or r.description ilike '%' || $2 || '%' "
            "       or coalesce(u.name, '') ilike '%' || $2 || '%')",
            cat, kw);

        Json::Value counts(Json::objectValue);
        counts["component"] = 0;
        counts["standard"] = 0;
        const auto countRows = db->execSqlSync(
            "select category, count(*)::bigint as n from market_resource "
            "where status = 1 group by category");
        for (const auto &row : countRows)
        {
            counts[row["category"].as<std::string>()] =
                static_cast<Json::Int64>(row["n"].as<int64_t>());
        }

        result.jsondata["items"] = items;
        result.jsondata["total"] = static_cast<Json::Int64>(totalRows[0]["total"].as<int64_t>());
        result.jsondata["counts"] = counts;
        result.jsondata["beta"] = true;
        result.setResult(ApiErrorCode::ApiError_Success, "success");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "MarketService::ListResources 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult MarketService::InitUpload(int userId,
                                     const std::string &name,
                                     const std::string &version,
                                     const std::string &description,
                                     const std::string &category,
                                     const std::string &fileName,
                                     int64_t fileSize)
{
    HttpResult result;
    const std::string safeName = Trim(name);
    const std::string safeVersion = Trim(version);
    const std::string safeDesc = Trim(description);
    const std::string safeFile = SafeFileName(fileName);

    if (safeName.empty() || safeName.size() > kMaxNameLength)
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "资源名称不能为空且不超过 80 字");
        return result;
    }
    if (safeVersion.size() > kMaxVersionLength)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "版本号不超过 32 字");
        return result;
    }
    if (safeDesc.size() > kMaxDescLength)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "说明不超过 1000 字");
        return result;
    }
    if (!IsAllowedCategory(category))
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "请选择元件库或标准库");
        return result;
    }
    if (safeFile.empty() || !IsAllowedExtension(safeFile))
    {
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "仅支持 .mdb / .db / .zip 文件");
        return result;
    }
    if (fileSize <= 0 || fileSize > kMaxFileSize)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "文件大小需在 200 MB 以内");
        return result;
    }

    auto db = drogon::app().getDbClient();
    try
    {
        CleanupStaleSessions(db, userId);

        const std::string uploadId = drogon::utils::getUuid();
        const std::string tempName = uploadId + ".part";
        const std::filesystem::path tempDir = UploadTempDir();
        const std::filesystem::path tempPath = tempDir / tempName;

        std::error_code ec;
        std::filesystem::create_directories(tempDir, ec);
        if (ec)
        {
            LOG_ERROR << "创建资源市场临时目录失败: " << tempDir << " " << ec.message();
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
                "insert into market_upload_session "
                "(id, user_id, name, version, description, category, file_name, file_size, "
                " temp_name, uploaded_bytes, status) "
                "values ($1, $2, $3, $4, $5, $6, $7, $8, $9, 0, 0)",
                uploadId, userId, safeName, safeVersion, safeDesc, category, safeFile,
                fileSize, tempName);
        }
        catch (...)
        {
            std::error_code removeEc;
            std::filesystem::remove(tempPath, removeEc);
            throw;
        }

        result.jsondata["uploadId"] = uploadId;
        result.jsondata["offset"] = static_cast<Json::Int64>(0);
        result.jsondata["chunkSize"] = static_cast<Json::Int64>(kChunkSize);
        result.setResult(ApiErrorCode::ApiError_Success, "upload session created");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "MarketService::InitUpload 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult MarketService::UploadChunk(int userId,
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
    if (data.empty() || data.size() > kMaxChunkBytes)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "分片为空或过大");
        return result;
    }

    auto db = drogon::app().getDbClient();
    try
    {
        std::lock_guard<std::mutex> fileLock(g_marketFileMutex);
        auto rows = db->execSqlSync(
            "select temp_name, file_size, status from market_upload_session "
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

        // 响应丢失后客户端会用旧 offset 重发：返回真实偏移让其重同步
        if (offset != actual)
        {
            result.jsondata["offset"] = static_cast<Json::Int64>(actual);
            result.setResult(ApiErrorCode::ApiError_Success, "offset resync");
            return result;
        }
        if (actual + static_cast<int64_t>(data.size()) > fileSize)
        {
            result.setResult(ApiErrorCode::ApiError_InvalidOperation, "分片超出文件大小");
            return result;
        }

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

        const int64_t newOffset = actual + static_cast<int64_t>(data.size());
        db->execSqlSync(
            "update market_upload_session set uploaded_bytes = $2, updated_at = now() "
            "where id = $1",
            uploadId, newOffset);

        result.jsondata["offset"] = static_cast<Json::Int64>(newOffset);
        result.setResult(ApiErrorCode::ApiError_Success, "chunk stored");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "MarketService::UploadChunk 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult MarketService::CompleteUpload(int userId, const std::string &uploadId)
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
        std::lock_guard<std::mutex> fileLock(g_marketFileMutex);
        auto rows = db->execSqlSync(
            "select name, version, description, category, file_name, file_size, temp_name, "
            "       status, resource_id "
            "from market_upload_session where id = $1 and user_id = $2",
            uploadId, userId);
        if (rows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_InvalidOperation, "上传会话不存在");
            return result;
        }

        const auto &session = rows[0];
        const int status = session["status"].as<int>();
        const int64_t resourceId = session["resource_id"].isNull()
                                       ? 0
                                       : session["resource_id"].as<int64_t>();

        // 重复 complete（上次响应丢失）直接返回已有记录
        if (status == 1 && resourceId > 0)
        {
            auto itemRows = db->execSqlSync(
                std::string(kItemSelect) + "where r.id = $2",
                userId, resourceId);
            if (!itemRows.empty())
            {
                FillItem(result.jsondata, itemRows[0]);
                result.setResult(ApiErrorCode::ApiError_Success, "上传已完成");
                return result;
            }
            result.setResult(ApiErrorCode::ApiError_InstallerNotFound, "资源不存在或已删除");
            return result;
        }

        const std::string safeName = session["name"].as<std::string>();
        const std::string category = session["category"].as<std::string>();
        const std::string safeFile = session["file_name"].as<std::string>();
        const int64_t fileSize = session["file_size"].as<int64_t>();
        const std::string tempName = session["temp_name"].as<std::string>();
        const auto tempPath = std::filesystem::path(UploadTempDir()) /
                              std::filesystem::path(tempName).filename();

        const int64_t actual = FileSizeOrZero(tempPath);
        if (actual != fileSize)
        {
            result.jsondata["offset"] = static_cast<Json::Int64>(actual);
            result.setResult(ApiErrorCode::ApiError_InvalidOperation,
                             "文件尚未上传完整，请继续上传");
            return result;
        }

        std::string sha256;
        if (!Sha256FileHex(tempPath, sha256))
        {
            result.setResult(ApiErrorCode::ApiError_InternalError, "计算文件校验值失败");
            return result;
        }

        const auto inserted = db->execSqlSync(
            "insert into market_resource "
            "(owner_user_id, owner_team_id, name, version, description, category, "
            " file_name, file_size, sha256, download_count, status) "
            "values ($1, (select team_id from team_member where user_id = $1 and status = 1 limit 1), "
            "        $2, $3, $4, $5, $6, $7, $8, 0, 1) returning id",
            userId, safeName, session["version"].as<std::string>(),
            session["description"].as<std::string>(), category, safeFile, fileSize, sha256);
        const int64_t newId = inserted[0]["id"].as<int64_t>();

        const std::filesystem::path destDir =
            std::filesystem::path(StoreDir()) / std::to_string(newId);
        std::error_code ec;
        std::filesystem::create_directories(destDir, ec);
        if (ec)
        {
            db->execSqlSync("delete from market_resource where id = $1", newId);
            LOG_ERROR << "创建资源目录失败: " << destDir << " " << ec.message();
            result.setResult(ApiErrorCode::ApiError_InternalError, "创建资源目录失败");
            return result;
        }
        const auto destPath = destDir / std::filesystem::path(safeFile).filename();
        std::filesystem::rename(tempPath, destPath, ec);
        if (ec)
        {
            std::error_code copyEc;
            std::filesystem::copy_file(tempPath, destPath,
                                       std::filesystem::copy_options::overwrite_existing, copyEc);
            if (copyEc)
            {
                db->execSqlSync("delete from market_resource where id = $1", newId);
                LOG_ERROR << "保存资源文件失败: " << destPath << " " << copyEc.message();
                result.setResult(ApiErrorCode::ApiError_InternalError, "保存资源文件失败");
                return result;
            }
            std::error_code removeEc;
            std::filesystem::remove(tempPath, removeEc);
        }

        db->execSqlSync(
            "update market_upload_session set status = 1, resource_id = $2, updated_at = now() "
            "where id = $1",
            uploadId, newId);

        auto itemRows = db->execSqlSync(std::string(kItemSelect) + "where r.id = $2",
                                        userId, newId);
        if (!itemRows.empty())
        {
            FillItem(result.jsondata, itemRows[0]);
        }
        result.setResult(ApiErrorCode::ApiError_Success, "上传成功");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "MarketService::CompleteUpload 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult MarketService::DeleteResource(int userId, int64_t resourceId)
{
    HttpResult result;
    if (resourceId <= 0)
    {
        result.setResult(ApiErrorCode::ApiError_InstallerNotFound, "资源不存在或已删除");
        return result;
    }

    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync(
            "select owner_user_id, status from market_resource where id = $1",
            resourceId);
        if (rows.empty() || rows[0]["status"].as<int>() != 1)
        {
            result.setResult(ApiErrorCode::ApiError_InstallerNotFound, "资源不存在或已删除");
            return result;
        }
        const int ownerId = rows[0]["owner_user_id"].as<int>();
        if (ownerId != userId && !IsAdminUser(db, userId))
        {
            result.setResult(ApiErrorCode::ApiError_InvalidOperation, "无权操作该资源");
            return result;
        }

        db->execSqlSync(
            "update market_resource set status = 0, updated_at = now() where id = $1",
            resourceId);
        std::error_code ec;
        std::filesystem::remove_all(
            std::filesystem::path(StoreDir()) / std::to_string(resourceId), ec);
        result.setResult(ApiErrorCode::ApiError_Success, "已删除");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "MarketService::DeleteResource 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

bool MarketService::ResolveDownload(int userId,
                                    int64_t resourceId,
                                    std::string &path,
                                    std::string &fileName,
                                    HttpResult &error)
{
    (void)userId;
    if (resourceId <= 0)
    {
        error.setResult(ApiErrorCode::ApiError_InstallerNotFound, "资源不存在或已删除");
        return false;
    }

    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync(
            "select file_name, status from market_resource where id = $1",
            resourceId);
        if (rows.empty() || rows[0]["status"].as<int>() != 1)
        {
            error.setResult(ApiErrorCode::ApiError_InstallerNotFound, "资源不存在或已删除");
            return false;
        }
        fileName = rows[0]["file_name"].as<std::string>();
        const auto filePath = std::filesystem::path(StoreDir()) / std::to_string(resourceId) /
                              std::filesystem::path(fileName).filename();
        std::error_code ec;
        if (!std::filesystem::exists(filePath, ec))
        {
            LOG_ERROR << "资源文件缺失: " << filePath;
            error.setResult(ApiErrorCode::ApiError_InstallerNotFound, "资源文件缺失");
            return false;
        }
        db->execSqlSync(
            "update market_resource set download_count = download_count + 1 where id = $1",
            resourceId);
        path = filePath.string();
        return true;
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "MarketService::ResolveDownload 失败: " << e.base().what();
        error.setResult(ApiErrorCode::ApiError_DatabaseError);
        return false;
    }
}

} // namespace Services
} // namespace UEAdminAPI

#include "FeedbackService.h"

#include <drogon/drogon.h>

#include "utils/ApiErrorCodes.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace drogon;
using namespace UEAdminAPI;
using namespace UEAdminAPI::utils;

namespace UEAdminAPI {
namespace Services {

namespace {

constexpr size_t kMaxContentBytes = 20000;
constexpr size_t kMaxContactBytes = 200;
constexpr size_t kMaxMetaBytes = 200;
constexpr size_t kMaxImageBytes = 12 * 1024 * 1024;
constexpr size_t kMaxImages = 5;
constexpr size_t kMaxTotalImageBytes = 24 * 1024 * 1024;

std::string StoreDir()
{
    const auto &cfg = drogon::app().getCustomConfig();
    std::string dir = "./uploads/feedback";
    if (cfg.isMember("Feedback") && cfg["Feedback"].isObject())
    {
        const auto &node = cfg["Feedback"]["StoreDir"];
        if (node.isString() && !node.asString().empty())
        {
            dir = node.asString();
        }
    }
    return dir;
}

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

std::string LowerMime(std::string mime)
{
    std::transform(mime.begin(), mime.end(), mime.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (mime == "image/jpg")
    {
        mime = "image/jpeg";
    }
    return mime;
}

bool ExtensionForMime(const std::string &mime, std::string &ext)
{
    if (mime == "image/png")
    {
        ext = ".png";
        return true;
    }
    if (mime == "image/jpeg")
    {
        ext = ".jpg";
        return true;
    }
    if (mime == "image/bmp")
    {
        ext = ".bmp";
        return true;
    }
    return false;
}

// 只允许位图，且按文件头校验，避免把任意文件存下来后被默认看图程序打开
bool MatchesImageMagic(const std::string &mime, std::string_view bytes)
{
    if (mime == "image/png")
    {
        static const unsigned char kPng[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        return bytes.size() >= sizeof(kPng) &&
               std::equal(kPng, kPng + sizeof(kPng), reinterpret_cast<const unsigned char *>(bytes.data()));
    }
    if (mime == "image/jpeg")
    {
        return bytes.size() >= 3 &&
               static_cast<unsigned char>(bytes[0]) == 0xFF &&
               static_cast<unsigned char>(bytes[1]) == 0xD8 &&
               static_cast<unsigned char>(bytes[2]) == 0xFF;
    }
    if (mime == "image/bmp")
    {
        return bytes.size() >= 2 && bytes[0] == 'B' && bytes[1] == 'M';
    }
    return false;
}

std::string CategoryName(int category)
{
    switch (category)
    {
    case 0:
        return "功能建议";
    case 1:
        return "问题报错";
    case 2:
        return "其他";
    default:
        return "其他";
    }
}

HttpResult ErrorResult(ApiErrorCode code, const std::string &msg)
{
    HttpResult result;
    result.setResult(code, msg);
    return result;
}

} // namespace

HttpResult FeedbackService::Submit(int userId,
                                   int category,
                                   const std::string &content,
                                   const std::string &contact,
                                   const std::string &appVersion,
                                   const std::string &osInfo,
                                   const std::vector<std::pair<std::string, std::string>> &images)
{
    const std::string trimmedContent = Trim(content);
    if (trimmedContent.empty())
    {
        return ErrorResult(ApiError_MissingRequiredArgs, "问题或建议不能为空");
    }
    if (trimmedContent.size() > kMaxContentBytes)
    {
        return ErrorResult(ApiError_InvalidOperation, "问题或建议过长，请精简后再提交");
    }
    if (category < 0 || category > 2)
    {
        category = 2;
    }
    if (contact.size() > kMaxContactBytes || appVersion.size() > kMaxMetaBytes ||
        osInfo.size() > kMaxMetaBytes)
    {
        return ErrorResult(ApiError_InvalidOperation, "联系方式或环境信息过长");
    }
    if (images.size() > kMaxImages)
    {
        return ErrorResult(ApiError_InvalidOperation, "最多上传 5 张截图");
    }

    std::vector<std::string> mimes(images.size());
    std::vector<std::string> exts(images.size());
    size_t totalImageBytes = 0;
    for (size_t i = 0; i < images.size(); ++i)
    {
        const std::string mime = LowerMime(images[i].first);
        std::string ext;
        if (!ExtensionForMime(mime, ext))
        {
            return ErrorResult(ApiError_InvalidOperation, "截图仅支持 PNG / JPG / BMP");
        }
        if (images[i].second.size() > kMaxImageBytes)
        {
            return ErrorResult(ApiError_InvalidOperation, "单张截图超过 12MB，请压缩后再试");
        }
        totalImageBytes += images[i].second.size();
        if (totalImageBytes > kMaxTotalImageBytes)
        {
            return ErrorResult(ApiError_InvalidOperation, "截图总大小超过 24MB，请精简后再试");
        }
        if (!MatchesImageMagic(mime, images[i].second))
        {
            return ErrorResult(ApiError_InvalidOperation, "截图内容与格式不符");
        }
        mimes[i] = mime;
        exts[i] = ext;
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
        LOG_ERROR << "FeedbackService::Submit 查询用户失败: " << e.base().what();
        return ErrorResult(ApiError_DatabaseError, "数据库错误");
    }

    int feedbackId = 0;
    try
    {
        auto rows = db->execSqlSync(
            "insert into user_feedback "
            "(user_id, user_name, category, content, contact, app_version, os_info) "
            "values ($1, $2, $3::int, $4, nullif($5, ''), nullif($6, ''), nullif($7, '')) "
            "returning id",
            userId, userName, category, trimmedContent, contact, appVersion, osInfo);
        if (rows.empty())
        {
            return ErrorResult(ApiError_DatabaseError, "保存反馈失败");
        }
        feedbackId = rows[0]["id"].as<int>();
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "FeedbackService::Submit 写入失败: " << e.base().what();
        return ErrorResult(ApiError_DatabaseError, "数据库错误");
    }

    const std::string dir = StoreDir();
    std::vector<std::filesystem::path> writtenFiles;
    try
    {
        if (!images.empty())
        {
            std::filesystem::create_directories(dir);
        }
        for (size_t i = 0; i < images.size(); ++i)
        {
            const std::string imageName =
                "feedback_" + std::to_string(feedbackId) + "_" + std::to_string(i) + exts[i];
            const std::filesystem::path fullPath = std::filesystem::path(dir) / imageName;
            {
                std::ofstream out(fullPath, std::ios::binary | std::ios::trunc);
                if (!out)
                {
                    throw std::runtime_error("open failed");
                }
                out.write(images[i].second.data(),
                          static_cast<std::streamsize>(images[i].second.size()));
                if (!out)
                {
                    throw std::runtime_error("write failed");
                }
            }
            writtenFiles.push_back(fullPath);
            db->execSqlSync(
                "insert into user_feedback_image (feedback_id, seq, image_name, image_mime) "
                "values ($1, $2::int, $3, $4)",
                feedbackId, static_cast<int>(i), imageName, mimes[i]);
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "FeedbackService::Submit 保存截图失败: " << e.what();
        for (size_t i = 0; i < writtenFiles.size(); ++i)
        {
            std::error_code ec;
            std::filesystem::remove(writtenFiles[i], ec);
        }
        try
        {
            db->execSqlSync("delete from user_feedback where id = $1", feedbackId);
        }
        catch (...)
        {
        }
        return ErrorResult(ApiError_InternalError, "截图保存失败，请重试");
    }

    HttpResult result;
    result.jsondata["feedbackId"] = feedbackId;
    result.jsondata["categoryName"] = CategoryName(category);
    result.jsondata["imageCount"] = static_cast<int>(images.size());
    result.setResult(ApiError_Success);
    return result;
}

HttpResult FeedbackService::AdminList(int status, int page, int size)
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

    auto db = drogon::app().getDbClient();
    try
    {
        auto totalRows = db->execSqlSync(
            "select count(*) as total from user_feedback where ($1::int < 0 or status = $1::int)", status);
        const long long total = totalRows.empty() ? 0 : totalRows[0]["total"].as<long long>();

        auto rows = db->execSqlSync(
            "select f.id, coalesce(f.user_id, 0) as user_id, f.user_name, f.category, f.content, "
            "       coalesce(f.contact, '') as contact, coalesce(f.app_version, '') as app_version, "
            "       coalesce(f.os_info, '') as os_info, "
            "       greatest((select count(*) from user_feedback_image i where i.feedback_id = f.id), "
            "                case when f.image_name is not null and f.image_name <> '' then 1 else 0 end) as image_count, "
            "       coalesce((select image_mime from user_feedback_image i "
            "                 where i.feedback_id = f.id order by seq limit 1), '') as image_mime, "
            "       f.status, "
            "       to_char(f.created_at, 'YYYY-MM-DD HH24:MI:SS') as created_at, "
            "       coalesce(to_char(f.handled_at, 'YYYY-MM-DD HH24:MI:SS'), '') as handled_at "
            "from user_feedback f "
            "where ($1::int < 0 or f.status = $1::int) "
            "order by f.id desc limit $2::int offset $3::int",
            status, size, offset);

        Json::Value items(Json::arrayValue);
        for (const auto &row : rows)
        {
            Json::Value item;
            item["id"] = row["id"].as<int>();
            item["userId"] = row["user_id"].as<int>();
            item["userName"] = row["user_name"].as<std::string>();
            item["category"] = row["category"].as<int>();
            item["categoryName"] = CategoryName(row["category"].as<int>());
            item["content"] = row["content"].as<std::string>();
            item["contact"] = row["contact"].as<std::string>();
            item["appVersion"] = row["app_version"].as<std::string>();
            item["osInfo"] = row["os_info"].as<std::string>();
            const int imageCount = row["image_count"].as<int>();
            item["hasImage"] = imageCount > 0;
            item["imageCount"] = imageCount;
            item["imageMime"] = row["image_mime"].as<std::string>();
            item["status"] = row["status"].as<int>();
            item["createdAt"] = row["created_at"].as<std::string>();
            item["handledAt"] = row["handled_at"].as<std::string>();
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
        LOG_ERROR << "FeedbackService::AdminList 查询失败: " << e.base().what();
        return ErrorResult(ApiError_DatabaseError, "数据库错误");
    }
}

HttpResult FeedbackService::AdminGetImage(int feedbackId, int index)
{
    if (index < 0 || index >= static_cast<int>(kMaxImages))
    {
        return ErrorResult(ApiError_InvalidOperation, "截图序号不合法");
    }

    auto db = drogon::app().getDbClient();
    std::string imageName;
    std::string mime;
    try
    {
        auto feedbackRows = db->execSqlSync(
            "select coalesce(image_name, '') as legacy_name, "
            "       coalesce(image_mime, '') as legacy_mime "
            "from user_feedback where id = $1",
            feedbackId);
        if (feedbackRows.empty())
        {
            return ErrorResult(ApiError_FeedbackNotFound, "反馈不存在");
        }

        auto rows = db->execSqlSync(
            "select image_name, coalesce(image_mime, '') as image_mime "
            "from user_feedback_image where feedback_id = $1 and seq = $2::int",
            feedbackId, index);
        if (!rows.empty())
        {
            imageName = rows[0]["image_name"].as<std::string>();
            mime = rows[0]["image_mime"].as<std::string>();
        }
        else if (index == 0)
        {
            // 兼容回填前/异常情况的旧单图字段
            imageName = feedbackRows[0]["legacy_name"].as<std::string>();
            mime = feedbackRows[0]["legacy_mime"].as<std::string>();
        }
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "FeedbackService::AdminGetImage 查询失败: " << e.base().what();
        return ErrorResult(ApiError_DatabaseError, "数据库错误");
    }

    if (imageName.empty())
    {
        return ErrorResult(ApiError_InvalidOperation, "该反馈没有截图");
    }

    const std::filesystem::path fullPath =
        std::filesystem::path(StoreDir()) / std::filesystem::path(imageName).filename();
    std::ifstream in(fullPath, std::ios::binary);
    if (!in)
    {
        LOG_ERROR << "FeedbackService::AdminGetImage 文件不存在: " << fullPath.string();
        return ErrorResult(ApiError_InternalError, "截图文件不存在");
    }
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    HttpResult result;
    result.jsondata["mime"] = mime;
    result.jsondata["index"] = index;
    result.jsondata["base64"] = drogon::utils::base64Encode(bytes);
    result.setResult(ApiError_Success);
    return result;
}

HttpResult FeedbackService::AdminMarkHandled(int feedbackId, int adminUserId)
{
    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync(
            "update user_feedback set status = 1, handled_at = now(), handled_by = $2 "
            "where id = $1 returning id",
            feedbackId, adminUserId);
        if (rows.empty())
        {
            return ErrorResult(ApiError_FeedbackNotFound, "反馈不存在");
        }
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "FeedbackService::AdminMarkHandled 更新失败: " << e.base().what();
        return ErrorResult(ApiError_DatabaseError, "数据库错误");
    }

    HttpResult result;
    result.setResult(ApiError_Success);
    return result;
}

} // namespace Services
} // namespace UEAdminAPI

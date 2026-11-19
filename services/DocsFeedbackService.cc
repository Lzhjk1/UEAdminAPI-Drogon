#include "DocsFeedbackService.h"

#include "utils/ApiErrorCodes.h"

#include <drogon/drogon.h>

using namespace drogon;
using namespace UEAdminAPI::utils;

namespace UEAdminAPI {
namespace Services {

namespace {

HttpResult ErrorResult(ApiErrorCode code, const std::string &message)
{
    HttpResult result;
    result.setResult(code, message);
    return result;
}

} // namespace

HttpResult DocsFeedbackService::Submit(const std::string &pagePath,
                                       const std::string &pageTitle,
                                       const std::string &sectionTitle,
                                       const std::string &selectedText,
                                       const std::string &content,
                                       const std::string &contact,
                                       const std::string &userAgent,
                                       const std::string &clientIp)
{
    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync(
            "insert into docs_feedback "
            "(page_path, page_title, section_title, selected_text, content, contact, user_agent, client_ip) "
            "values ($1, $2, $3, $4, $5, $6, $7, $8) returning id",
            pagePath, pageTitle, sectionTitle, selectedText, content, contact, userAgent, clientIp);

        HttpResult result;
        if (!rows.empty())
        {
            result.jsondata["feedbackId"] = rows[0]["id"].as<int>();
        }
        result.setResult(ApiError_Success);
        return result;
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "DocsFeedbackService::Submit 写入失败: " << e.base().what();
        return ErrorResult(ApiError_DatabaseError, "数据库错误");
    }
}

HttpResult DocsFeedbackService::AdminList(int status, int page, int size)
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
            "select count(*) as total from docs_feedback where ($1::int < 0 or status = $1::int)",
            status);
        const long long total = totalRows.empty() ? 0 : totalRows[0]["total"].as<long long>();

        auto rows = db->execSqlSync(
            "select id, page_path, coalesce(page_title, '') as page_title, "
            "       coalesce(section_title, '') as section_title, "
            "       coalesce(selected_text, '') as selected_text, content, "
            "       coalesce(contact, '') as contact, coalesce(user_agent, '') as user_agent, "
            "       coalesce(client_ip, '') as client_ip, status, "
            "       coalesce(admin_note, '') as admin_note, "
            "       to_char(created_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD HH24:MI:SS') as created_at, "
            "       coalesce(to_char(handled_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD HH24:MI:SS'), '') as handled_at "
            "from docs_feedback "
            "where ($1::int < 0 or status = $1::int) "
            "order by id desc limit $2::int offset $3::int",
            status, size, offset);

        Json::Value items(Json::arrayValue);
        for (const auto &row : rows)
        {
            Json::Value item;
            item["id"] = row["id"].as<int>();
            item["pagePath"] = row["page_path"].as<std::string>();
            item["pageTitle"] = row["page_title"].as<std::string>();
            item["sectionTitle"] = row["section_title"].as<std::string>();
            item["selectedText"] = row["selected_text"].as<std::string>();
            item["content"] = row["content"].as<std::string>();
            item["contact"] = row["contact"].as<std::string>();
            item["userAgent"] = row["user_agent"].as<std::string>();
            item["clientIp"] = row["client_ip"].as<std::string>();
            item["status"] = row["status"].as<int>();
            item["adminNote"] = row["admin_note"].as<std::string>();
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
        LOG_ERROR << "DocsFeedbackService::AdminList 查询失败: " << e.base().what();
        return ErrorResult(ApiError_DatabaseError, "数据库错误");
    }
}

HttpResult DocsFeedbackService::AdminHandle(int feedbackId,
                                            int status,
                                            const std::string &note,
                                            int adminUserId)
{
    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync(
            "update docs_feedback set status = $2, "
            "       admin_note = coalesce(nullif($3::text, ''), admin_note), "
            "       handled_at = now(), handled_by = $4 "
            "where id = $1 returning id",
            feedbackId, status, note, adminUserId);
        if (rows.empty())
        {
            return ErrorResult(ApiError_FeedbackNotFound, "反馈不存在");
        }
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "DocsFeedbackService::AdminHandle 更新失败: " << e.base().what();
        return ErrorResult(ApiError_DatabaseError, "数据库错误");
    }

    HttpResult result;
    result.setResult(ApiError_Success);
    return result;
}

} // namespace Services
} // namespace UEAdminAPI

#include "DocsFeedbackController.h"

#include "services/DocsFeedbackService.h"
#include "utils/ApiErrorCodes.h"
#include "utils/HttpResult.h"

#include <string>

using namespace drogon;
using namespace UEAdminAPI;
using namespace UEAdminAPI::Services;
using namespace UEAdminAPI::utils;

namespace {

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

int ParseInt(const std::string &value, int fallback)
{
    try
    {
        return std::stoi(value);
    }
    catch (...)
    {
        return fallback;
    }
}

std::string JsonString(const Json::Value &json, const char *key)
{
    if (json.isMember(key) && json[key].isString())
    {
        return json[key].asString();
    }
    return std::string();
}

} // namespace

Task<HttpResponsePtr> DocsFeedbackController::submit(HttpRequestPtr req)
{
    HttpResult result;
    auto json = req->getJsonObject();
    if (!json)
    {
        result.setResult(ApiError_InvalidJsonFormat);
        co_return JsonResponse(result);
    }

    // 简单蜜罐：正常用户不会填写，机器人填充后直接假装成功
    if (!JsonString(*json, "website").empty())
    {
        result.setResult(ApiError_Success);
        co_return JsonResponse(result);
    }

    const std::string pagePath = JsonString(*json, "pagePath");
    const std::string pageTitle = JsonString(*json, "pageTitle");
    const std::string sectionTitle = JsonString(*json, "sectionTitle");
    const std::string selectedText = JsonString(*json, "selectedText");
    const std::string content = JsonString(*json, "content");
    const std::string contact = JsonString(*json, "contact");

    if (pagePath.empty() || content.empty())
    {
        result.setResult(ApiError_MissingRequiredArgs, "缺少 pagePath 或 content");
        co_return JsonResponse(result);
    }
    if (pagePath.size() > 300 || pageTitle.size() > 300 || sectionTitle.size() > 300 ||
        selectedText.size() > 4000 || content.size() > 2000 || contact.size() > 200)
    {
        result.setResult(ApiError_InvalidOperation, "反馈内容超出长度限制");
        co_return JsonResponse(result);
    }
    if (pagePath.rfind("/site/docs/", 0) != 0 && pagePath.rfind("/docs/", 0) != 0)
    {
        result.setResult(ApiError_InvalidOperation, "页面路径不合法");
        co_return JsonResponse(result);
    }

    std::string userAgent = req->getHeader("user-agent");
    if (userAgent.size() > 500)
    {
        userAgent.resize(500);
    }
    const std::string clientIp = req->peerAddr().toIp();

    result = DocsFeedbackService::Submit(pagePath, pageTitle, sectionTitle, selectedText, content,
                                         contact, userAgent, clientIp);
    co_return JsonResponse(result);
}

Task<HttpResponsePtr> DocsFeedbackController::adminList(HttpRequestPtr req)
{
    const std::string statusText = req->getParameter("status");
    const std::string pageText = req->getParameter("page");
    const std::string sizeText = req->getParameter("size");

    const int status = statusText.empty() ? -1 : ParseInt(statusText, -1);
    const int page = pageText.empty() ? 1 : ParseInt(pageText, 1);
    const int size = sizeText.empty() ? 50 : ParseInt(sizeText, 50);

    co_return JsonResponse(DocsFeedbackService::AdminList(status, page, size));
}

Task<HttpResponsePtr> DocsFeedbackController::adminHandle(HttpRequestPtr req,
                                                          std::string feedbackId)
{
    HttpResult result;
    auto json = req->getJsonObject();
    if (!json)
    {
        result.setResult(ApiError_InvalidJsonFormat);
        co_return JsonResponse(result);
    }

    int status = 1;
    if (json->isMember("status"))
    {
        const auto &node = (*json)["status"];
        status = node.isInt() ? node.asInt()
                              : ParseInt(node.isString() ? node.asString() : "", 1);
    }
    if (status != 1 && status != 2)
    {
        result.setResult(ApiError_InvalidOperation, "status 只能是 1（已修正）或 2（忽略）");
        co_return JsonResponse(result);
    }

    std::string note = JsonString(*json, "note");
    if (note.size() > 1000)
    {
        note.resize(1000);
    }

    const int id = ParseInt(feedbackId, -1);
    if (id <= 0)
    {
        result.setResult(ApiError_InvalidOperation, "反馈 ID 不合法");
        co_return JsonResponse(result);
    }

    const int adminUserId = CurrentUserId(req);
    co_return JsonResponse(DocsFeedbackService::AdminHandle(id, status, note, adminUserId));
}

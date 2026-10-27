#include "FeedbackController.h"

#include "services/FeedbackService.h"
#include "utils/ApiErrorCodes.h"
#include "utils/HttpResult.h"

#include <drogon/utils/Utilities.h>

#include <utility>
#include <vector>

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

Task<HttpResponsePtr> FeedbackController::submit(HttpRequestPtr req)
{
    HttpResult result;
    const int userId = CurrentUserId(req);
    if (userId <= 0)
    {
        result.setResult(ApiError_TokenMissing, "缺少登录态");
        co_return JsonResponse(result);
    }

    auto json = req->getJsonObject();
    if (!json)
    {
        result.setResult(ApiError_InvalidJsonFormat);
        co_return JsonResponse(result);
    }

    const std::string content = JsonString(*json, "content");
    if (content.empty())
    {
        result.setResult(ApiError_MissingRequiredArgs, "缺少 content");
        co_return JsonResponse(result);
    }

    const std::string contact = JsonString(*json, "contact");
    const std::string appVersion = JsonString(*json, "appVersion");
    const std::string osInfo = JsonString(*json, "osInfo");
    int category = 0;
    if (json->isMember("category"))
    {
        const auto &node = (*json)["category"];
        category = node.isInt() ? node.asInt() : ParseInt(node.isString() ? node.asString() : "", 0);
    }

    std::vector<std::pair<std::string, std::string>> images;
    if (json->isMember("images") && (*json)["images"].isArray())
    {
        const auto &array = (*json)["images"];
        if (array.size() > 5)
        {
            result.setResult(ApiError_InvalidOperation, "最多上传 5 张截图");
            co_return JsonResponse(result);
        }
        for (Json::ArrayIndex i = 0; i < array.size(); ++i)
        {
            const auto &node = array[i];
            if (!node.isObject())
            {
                continue;
            }
            const std::string mime = JsonString(node, "mime");
            const std::string base64 = JsonString(node, "base64");
            if (base64.empty())
            {
                continue;
            }
            std::string bytes = drogon::utils::base64Decode(base64);
            if (bytes.empty())
            {
                result.setResult(ApiError_InvalidOperation, "截图数据无法解码");
                co_return JsonResponse(result);
            }
            images.push_back(std::make_pair(mime, std::move(bytes)));
        }
    }
    else
    {
        // 兼容旧客户端的单图字段
        const std::string imageMime = JsonString(*json, "imageMime");
        const std::string imageBase64 = JsonString(*json, "imageBase64");
        if (!imageBase64.empty())
        {
            std::string bytes = drogon::utils::base64Decode(imageBase64);
            if (bytes.empty())
            {
                result.setResult(ApiError_InvalidOperation, "截图数据无法解码");
                co_return JsonResponse(result);
            }
            images.push_back(std::make_pair(imageMime, std::move(bytes)));
        }
    }

    co_return JsonResponse(FeedbackService::Submit(
        userId, category, content, contact, appVersion, osInfo, images));
}

Task<HttpResponsePtr> FeedbackController::list(HttpRequestPtr req)
{
    const int status = ParseInt(req->getParameter("status"), -1);
    const int page = ParseInt(req->getParameter("page"), 1);
    const int size = ParseInt(req->getParameter("size"), 50);
    co_return JsonResponse(FeedbackService::AdminList(status, page, size));
}

Task<HttpResponsePtr> FeedbackController::image(HttpRequestPtr req, std::string feedbackId)
{
    const int id = ParseInt(feedbackId, 0);
    const int index = ParseInt(req->getParameter("index"), 0);
    if (id <= 0)
    {
        HttpResult result;
        result.setResult(ApiError_FeedbackNotFound);
        co_return JsonResponse(result);
    }
    co_return JsonResponse(FeedbackService::AdminGetImage(id, index));
}

Task<HttpResponsePtr> FeedbackController::handle(HttpRequestPtr req, std::string feedbackId)
{
    const int id = ParseInt(feedbackId, 0);
    HttpResult result;
    if (id <= 0)
    {
        result.setResult(ApiError_FeedbackNotFound);
        co_return JsonResponse(result);
    }
    co_return JsonResponse(FeedbackService::AdminMarkHandled(id, CurrentUserId(req)));
}

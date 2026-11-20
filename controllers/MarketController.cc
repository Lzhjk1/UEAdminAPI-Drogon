#include "MarketController.h"

#include "services/MarketService.h"
#include "utils/ApiErrorCodes.h"
#include "utils/HttpResult.h"

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

int64_t ParseInt64(const std::string &value, int64_t fallback)
{
    try
    {
        return std::stoll(value);
    }
    catch (...)
    {
        return fallback;
    }
}

constexpr size_t kMaxChunkBytes = 8 * 1024 * 1024;

std::string JsonString(const Json::Value &json, const char *key)
{
    if (json.isMember(key) && json[key].isString())
    {
        return json[key].asString();
    }
    return std::string();
}

int64_t JsonInt64(const Json::Value &json, const char *key, int64_t fallback)
{
    if (!json.isMember(key))
    {
        return fallback;
    }
    const auto &node = json[key];
    if (node.isInt64())
    {
        return node.asInt64();
    }
    if (node.isUInt64())
    {
        return static_cast<int64_t>(node.asUInt64());
    }
    if (node.isString())
    {
        return ParseInt64(node.asString(), fallback);
    }
    return fallback;
}

} // namespace

Task<HttpResponsePtr> MarketController::list(HttpRequestPtr req)
{
    const std::string category = req->getParameter("category");
    const std::string keyword = req->getParameter("keyword");
    const int page = static_cast<int>(ParseInt64(req->getParameter("page"), 1));
    const int pageSize = static_cast<int>(ParseInt64(req->getParameter("pageSize"), 200));
    co_return JsonResponse(MarketService::ListResources(
        CurrentUserId(req), category, keyword, page, pageSize));
}

Task<HttpResponsePtr> MarketController::download(HttpRequestPtr req, std::string resourceId)
{
    HttpResult error;
    std::string path;
    std::string fileName;
    const int64_t id = ParseInt64(resourceId, 0);
    if (!MarketService::ResolveDownload(CurrentUserId(req), id, path, fileName, error))
    {
        co_return JsonResponse(error);
    }

    auto resp = HttpResponse::newFileResponse(path);
    resp->setStatusCode(k200OK);
    resp->addHeader("Content-Disposition", "attachment; filename=\"" + fileName + "\"");
    co_return resp;
}

Task<HttpResponsePtr> MarketController::uploadInit(HttpRequestPtr req)
{
    HttpResult result;
    auto json = req->getJsonObject();
    if (!json)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidJsonFormat);
        co_return JsonResponse(result);
    }

    co_return JsonResponse(MarketService::InitUpload(
        CurrentUserId(req),
        JsonString(*json, "name"),
        JsonString(*json, "version"),
        JsonString(*json, "description"),
        JsonString(*json, "category"),
        JsonString(*json, "fileName"),
        JsonInt64(*json, "fileSize", 0)));
}

Task<HttpResponsePtr> MarketController::uploadChunk(HttpRequestPtr req, std::string uploadId)
{
    HttpResult result;
    const int64_t offset = ParseInt64(req->getParameter("offset"), -1);
    const auto body = req->getBody();
    if (offset < 0)
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "缺少 offset");
        co_return JsonResponse(result);
    }
    if (body.empty() || body.size() > kMaxChunkBytes)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "分片为空或过大");
        co_return JsonResponse(result);
    }

    co_return JsonResponse(MarketService::UploadChunk(
        CurrentUserId(req), uploadId, offset,
        std::string_view(body.data(), body.size())));
}

Task<HttpResponsePtr> MarketController::uploadComplete(HttpRequestPtr req, std::string uploadId)
{
    (void)req;
    co_return JsonResponse(MarketService::CompleteUpload(CurrentUserId(req), uploadId));
}

Task<HttpResponsePtr> MarketController::remove(HttpRequestPtr req, std::string resourceId)
{
    (void)req;
    HttpResult result;
    const int64_t id = ParseInt64(resourceId, 0);
    if (id <= 0)
    {
        result.setResult(ApiErrorCode::ApiError_InstallerNotFound, "资源不存在或已删除");
        co_return JsonResponse(result);
    }
    co_return JsonResponse(MarketService::DeleteResource(CurrentUserId(req), id));
}

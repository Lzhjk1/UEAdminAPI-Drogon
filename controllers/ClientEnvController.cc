#include "ClientEnvController.h"

#include "services/ClientEnvService.h"
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

} // namespace

Task<HttpResponsePtr> ClientEnvController::submit(HttpRequestPtr req)
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

    co_return JsonResponse(ClientEnvService::Submit(userId, *json));
}

Task<HttpResponsePtr> ClientEnvController::list(HttpRequestPtr req)
{
    const std::string keyword = req->getParameter("keyword");
    const int page = ParseInt(req->getParameter("page"), 1);
    const int size = ParseInt(req->getParameter("size"), 50);
    co_return JsonResponse(ClientEnvService::AdminList(keyword, page, size));
}

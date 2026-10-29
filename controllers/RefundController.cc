#include "RefundController.h"

#include "services/RefundService.h"
#include "utils/ApiErrorCodes.h"
#include "utils/HttpResult.h"

using namespace drogon;
using namespace UEAdminAPI;
using namespace UEAdminAPI::Services;

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

HttpResponsePtr JsonResponse(const UEAdminAPI::utils::HttpResult &result)
{
    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    return resp;
}

int ToInt(const std::string &value, int fallback)
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

bool ToId(const std::string &value, long long &id)
{
    try
    {
        id = std::stoll(value);
        return id > 0;
    }
    catch (...)
    {
        return false;
    }
}

std::string JsonString(const HttpRequestPtr &req, const char *key)
{
    auto json = req->getJsonObject();
    if (json && json->isMember(key) && (*json)[key].isString())
    {
        return (*json)[key].asString();
    }
    return "";
}

} // namespace

Task<HttpResponsePtr> RefundController::apply(HttpRequestPtr req, std::string orderNo)
{
    UEAdminAPI::utils::HttpResult result =
        RefundService::Apply(CurrentUserId(req), orderNo, JsonString(req, "reason"));
    co_return JsonResponse(result);
}

Task<HttpResponsePtr> RefundController::adminList(HttpRequestPtr req)
{
    const int status = ToInt(req->getParameter("status"), -1);
    const int page = ToInt(req->getParameter("page"), 1);
    const int size = ToInt(req->getParameter("size"), 50);
    const std::string keyword = req->getParameter("keyword");

    UEAdminAPI::utils::HttpResult result = RefundService::AdminList(status, keyword, page, size);
    co_return JsonResponse(result);
}

Task<HttpResponsePtr> RefundController::adminGet(HttpRequestPtr req, std::string id)
{
    (void)req;
    long long refundId = 0;
    if (!ToId(id, refundId))
    {
        UEAdminAPI::utils::HttpResult result;
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "退款单 ID 不合法");
        co_return JsonResponse(result);
    }
    co_return JsonResponse(RefundService::AdminGet(refundId));
}

Task<HttpResponsePtr> RefundController::adminApprove(HttpRequestPtr req, std::string id)
{
    long long refundId = 0;
    if (!ToId(id, refundId))
    {
        UEAdminAPI::utils::HttpResult result;
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "退款单 ID 不合法");
        co_return JsonResponse(result);
    }
    UEAdminAPI::utils::HttpResult result =
        co_await RefundService::AdminApprove(CurrentUserId(req), refundId, JsonString(req, "remark"));
    co_return JsonResponse(result);
}

Task<HttpResponsePtr> RefundController::adminReject(HttpRequestPtr req, std::string id)
{
    long long refundId = 0;
    if (!ToId(id, refundId))
    {
        UEAdminAPI::utils::HttpResult result;
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "退款单 ID 不合法");
        co_return JsonResponse(result);
    }
    UEAdminAPI::utils::HttpResult result =
        RefundService::AdminReject(CurrentUserId(req), refundId, JsonString(req, "remark"));
    co_return JsonResponse(result);
}

Task<HttpResponsePtr> RefundController::adminRollback(HttpRequestPtr req, std::string id)
{
    long long refundId = 0;
    if (!ToId(id, refundId))
    {
        UEAdminAPI::utils::HttpResult result;
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "退款单 ID 不合法");
        co_return JsonResponse(result);
    }
    UEAdminAPI::utils::HttpResult result = RefundService::AdminRollback(CurrentUserId(req), refundId);
    co_return JsonResponse(result);
}

Task<HttpResponsePtr> RefundController::adminSync(HttpRequestPtr req, std::string id)
{
    (void)req;
    long long refundId = 0;
    if (!ToId(id, refundId))
    {
        UEAdminAPI::utils::HttpResult result;
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "退款单 ID 不合法");
        co_return JsonResponse(result);
    }
    UEAdminAPI::utils::HttpResult result = co_await RefundService::AdminSync(refundId);
    co_return JsonResponse(result);
}

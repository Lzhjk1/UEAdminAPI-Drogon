#include "BillController.h"

#include "services/BillService.h"
#include "utils/ApiErrorCodes.h"
#include "utils/HttpResult.h"

#include <drogon/utils/Utilities.h>

#include <cstdlib>
#include <map>
#include <optional>
#include <string>

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

std::optional<int64_t> ParseAmountCents(const HttpRequestPtr &req)
{
    auto reqJson = req->getJsonObject();
    if (!reqJson)
    {
        return std::nullopt;
    }
    const auto &amountNode = (*reqJson)["amountCents"];
    if (amountNode.isInt64() || amountNode.isInt() || amountNode.isUInt())
    {
        return amountNode.asInt64();
    }
    if (amountNode.isString())
    {
        try
        {
            return std::stoll(amountNode.asString());
        }
        catch (...)
        {
            return static_cast<int64_t>(0);
        }
    }
    return static_cast<int64_t>(0);
}

int ToInt(const std::string &value, int fallback)
{
    if (value.empty())
    {
        return fallback;
    }
    try
    {
        return std::stoi(value);
    }
    catch (...)
    {
        return fallback;
    }
}

std::map<std::string, std::string> RequestHeaders(const HttpRequestPtr &req)
{
    std::map<std::string, std::string> headers;
    for (const auto &[key, value] : req->headers())
    {
        headers[key] = value;
    }
    return headers;
}

// 240x240 的二维码占位图（模拟支付 / 订单尚未生成渠道二维码时使用）
const char *kQrPlaceholderBase64 =
    "iVBORw0KGgoAAAANSUhEUgAAAPAAAADwCAAAAAAbPrZOAAABzElEQVR42u3bQRLCMAgAwP7/03p3xikkoYa43Fq1sHooKfF6/VlcwMDAwMDAwMDAwMDAwMDAwMBHgq/B+HadVedn6wEGBgYGBu4EDt/MP95/9/ns67P1AAMDAwMDdwbf3dijgEhjUF0PMDAwMDDw6eAsKJoXGBgYGBgYON94jMKBgYGBgYGB8wVGrxc9v+3kARgYGBgYuBC8ckNYBNhuJx4wMDAwMHABeNVCfbZhqcgLDAwMDAzcBVw96B49nvqRgIGBgYGBNwevHnBnF/KvwgAGBgYGBt4dHP0CupwHBgYGBgY+ERwtYLTA2bzAwMDAwMAdwR0H3dG8wMDAwMDAu4OzCVYdV+UFBgYGBgY+EXzXMFQPuh//oxYwMDAwMPAD4O4DcGBgYGBg4M7g6AV/Negu25gGDAwMDAy8ETibMLtQr84LDAwMDAzcEXwNRqYBiDQcFQN2YGBgYGDgLuDRQmejcsAODAwMDAzcDRxtNHYcgAMDAwMDA/87OFpw1YAdGBgYGBgY+H6hP/uAABgYGBgY+ETw7IP4JwbdW00egIGBgYGBF4BHN6ZlG4aVG8CBgYGBgYG7g08OYGBgYGBgYGBgYGBgYGBgYOD28QYxpCa/Y/xS5gAAAABJRU5ErkJggg==";

} // namespace

Task<HttpResponsePtr> BillController::createOrder(HttpRequestPtr req)
{
    UEAdminAPI::utils::HttpResult result;
    auto amountOpt = ParseAmountCents(req);
    if (!amountOpt)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidJsonFormat);
        co_return JsonResponse(result);
    }
    const int64_t amountCents = *amountOpt;

    auto reqJson = req->getJsonObject();
    // 缺省 0 会被 CreateOrder 判为不支持，避免旧请求误落模拟渠道
    int channel = 0;
    const auto &channelNode = (*reqJson)["channel"];
    if (channelNode.isInt())
    {
        channel = channelNode.asInt();
    }
    else if (channelNode.isString())
    {
        channel = ToInt(channelNode.asString(), 0);
    }

    const std::string idemKey = (*reqJson)["idemKey"].asString();

    result = co_await BillService::CreateOrder(CurrentUserId(req), amountCents, channel, idemKey);
    co_return JsonResponse(result);
}

Task<HttpResponsePtr> BillController::createOrderGroup(HttpRequestPtr req)
{
    UEAdminAPI::utils::HttpResult result;
    auto amountOpt = ParseAmountCents(req);
    if (!amountOpt)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidJsonFormat);
        co_return JsonResponse(result);
    }

    const std::string idemKey = (*req->getJsonObject())["idemKey"].asString();
    result = co_await BillService::CreateOrderGroup(CurrentUserId(req), *amountOpt, idemKey);
    co_return JsonResponse(result);
}

Task<HttpResponsePtr> BillController::getOrderGroup(HttpRequestPtr req, std::string groupNo)
{
    UEAdminAPI::utils::HttpResult result =
        co_await BillService::GetOrderGroup(CurrentUserId(req), groupNo);
    co_return JsonResponse(result);
}

Task<HttpResponsePtr> BillController::getOrder(HttpRequestPtr req, std::string orderNo)
{
    UEAdminAPI::utils::HttpResult result = co_await BillService::GetOrder(CurrentUserId(req), orderNo);
    co_return JsonResponse(result);
}

Task<HttpResponsePtr> BillController::listOrders(HttpRequestPtr req)
{
    const int page = ToInt(req->getParameter("page"), 1);
    const int size = ToInt(req->getParameter("size"), 20);

    UEAdminAPI::utils::HttpResult result = BillService::ListOrders(CurrentUserId(req), page, size);
    co_return JsonResponse(result);
}

Task<HttpResponsePtr> BillController::mockPaid(HttpRequestPtr req, std::string orderNo)
{
    UEAdminAPI::utils::HttpResult result = BillService::MockPaid(CurrentUserId(req), orderNo);
    co_return JsonResponse(result);
}

Task<HttpResponsePtr> BillController::listTransactions(HttpRequestPtr req)
{
    const int type = ToInt(req->getParameter("type"), 0);
    const int page = ToInt(req->getParameter("page"), 1);
    const int size = ToInt(req->getParameter("size"), 50);

    UEAdminAPI::utils::HttpResult result = BillService::ListTransactions(CurrentUserId(req), type, page, size);
    co_return JsonResponse(result);
}

Task<HttpResponsePtr> BillController::qrPlaceholder(HttpRequestPtr req, std::string orderNo)
{
    (void)req;
    std::string png;
    if (!BillService::GetQrPng(orderNo, png))
    {
        png = drogon::utils::base64Decode(kQrPlaceholderBase64);
    }
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    resp->setContentTypeCode(CT_IMAGE_PNG);
    resp->addHeader("Cache-Control", "no-store");
    resp->setBody(std::move(png));
    co_return resp;
}

Task<HttpResponsePtr> BillController::alipayNotify(HttpRequestPtr req)
{
    const std::string body(req->body().data(), req->body().size());
    const NotifyReply reply = BillService::HandleAlipayNotify(body, RequestHeaders(req));

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    resp->setContentTypeString("text/plain;charset=utf-8");
    resp->setBody(reply.ok ? "success" : "failure");
    co_return resp;
}

Task<HttpResponsePtr> BillController::wechatNotify(HttpRequestPtr req)
{
    const std::string body(req->body().data(), req->body().size());
    const NotifyReply reply = BillService::HandleWechatNotify(body, RequestHeaders(req));

    Json::Value json;
    json["code"] = reply.ok ? "SUCCESS" : "FAIL";
    json["message"] = reply.message;
    auto resp = HttpResponse::newHttpJsonResponse(json);
    resp->setStatusCode(reply.ok ? k200OK : k400BadRequest);
    co_return resp;
}

#include "BillController.h"

#include "services/BillService.h"
#include "utils/ApiErrorCodes.h"
#include "utils/HttpResult.h"

#include <drogon/utils/Utilities.h>

#include <cstdlib>
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

// 240x240 的二维码占位图（模拟支付阶段使用；真实接入后改由支付渠道返回的 code_url 渲染）
const char *kQrPlaceholderBase64 =
    "iVBORw0KGgoAAAANSUhEUgAAAPAAAADwCAAAAAAbPrZOAAABzElEQVR42u3bQRLCMAgAwP7/03p3xikkoYa43Fq1sHooKfF6/VlcwMDAwMDAwMDAwMDAwMDAwMBHgq/B+HadVedn6wEGBgYGBu4EDt/MP95/9/ns67P1AAMDAwMDdwbf3dijgEhjUF0PMDAwMDDw6eAsKJoXGBgYGBgYON94jMKBgYGBgYGB8wVGrxc9v+3kARgYGBgYuBC8ckNYBNhuJx4wMDAwMHABeNVCfbZhqcgLDAwMDAzcBVw96B49nvqRgIGBgYGBNwevHnBnF/KvwgAGBgYGBt4dHP0CupwHBgYGBgY+ERwtYLTA2bzAwMDAwMAdwR0H3dG8wMDAwMDAu4OzCVYdV+UFBgYGBgY+EXzXMFQPuh//oxYwMDAwMPAD4O4DcGBgYGBg4M7g6AV/Negu25gGDAwMDAy8ETibMLtQr84LDAwMDAzcEXwNRqYBiDQcFQN2YGBgYGDgLuDRQmejcsAODAwMDAzcDRxtNHYcgAMDAwMDA/87OFpw1YAdGBgYGBgY+H6hP/uAABgYGBgY+ETw7IP4JwbdW00egIGBgYGBF4BHN6ZlG4aVG8CBgYGBgYG7g08OYGBgYGBgYGBgYGBgYGBgYOD28QYxpCa/Y/xS5gAAAABJRU5ErkJggg==";

} // namespace

Task<HttpResponsePtr> BillController::createOrder(HttpRequestPtr req)
{
    UEAdminAPI::utils::HttpResult result;
    auto reqJson = req->getJsonObject();
    if (!reqJson)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidJsonFormat);
        co_return JsonResponse(result);
    }

    int64_t amountCents = 0;
    const auto &amountNode = (*reqJson)["amountCents"];
    if (amountNode.isInt64() || amountNode.isInt() || amountNode.isUInt())
    {
        amountCents = amountNode.asInt64();
    }
    else if (amountNode.isString())
    {
        try
        {
            amountCents = std::stoll(amountNode.asString());
        }
        catch (...)
        {
            amountCents = 0;
        }
    }

    int channel = 9;
    const auto &channelNode = (*reqJson)["channel"];
    if (channelNode.isInt())
    {
        channel = channelNode.asInt();
    }
    else if (channelNode.isString())
    {
        channel = ToInt(channelNode.asString(), 9);
    }

    const std::string idemKey = (*reqJson)["idemKey"].asString();

    result = BillService::CreateOrder(CurrentUserId(req), amountCents, channel, idemKey);
    co_return JsonResponse(result);
}

Task<HttpResponsePtr> BillController::getOrder(HttpRequestPtr req, std::string orderNo)
{
    UEAdminAPI::utils::HttpResult result = BillService::GetOrder(CurrentUserId(req), orderNo);
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
    (void)orderNo;
    const std::string bytes = drogon::utils::base64Decode(kQrPlaceholderBase64);
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    resp->setContentTypeCode(CT_IMAGE_PNG);
    resp->setBody(bytes);
    co_return resp;
}

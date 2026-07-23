#include "TestMfaController.h"
#include "utils/HttpResult.h"
#include "utils/ApiErrorCodes.h"

using namespace drogon;
using namespace UEAdminAPI;
using namespace UEAdminAPI::utils;

Task<HttpResponsePtr> TestMfaController::GetLatestCode(HttpRequestPtr req, std::string target, std::string type) {
    HttpResult result;

    // 非测试模式一律拒绝, 避免生产环境泄露验证码
    if (!TestModeConfig::Enable()) {
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "测试模式未开启, 该接口不可用");
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k403Forbidden);
        co_return resp;
    }

    if (target.empty() || type.empty()) {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "缺少参数 target 或 type");
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        co_return resp;
    }

    auto _mfaService = MFAService::Instance();
    auto code = _mfaService->GetLatestCode(target, stringToMFAType(type));

    if (!code) {
        result.setResult(ApiErrorCode::ApiError_InvalidVerifyCode, "验证码不存在或已过期");
        result.jsondata["code"] = "";
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        co_return resp;
    }

    result.setResult(ApiErrorCode::ApiError_Success, "查询成功");
    result.jsondata["code"] = *code;
    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    co_return resp;
}
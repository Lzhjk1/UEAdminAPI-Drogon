#include "UserController.h"
#include <utils/PostParamMap.h>
#include <utils/DataFormatUtils.h>
#include <utils/HttpResult.h>


#include "services/ThirdPartyLoginService.h"
#include "services/AuthService.h"
#include "services/ActionTokenService.h"
#include "services/MFAService.h"

#include <numeric>

using namespace drogon;
using namespace UEAdminAPI;
using namespace UEAdminAPI::utils;
using namespace UEAdminAPI::Services;


Task<HttpResponsePtr> UserController::updateUser(HttpRequestPtr req) {
    auto _authService = AuthService::Instance();
    HttpResult result;

    auto reqJson = req->getJsonObject();
    if (!reqJson) {
        result.setResult(ApiErrorCode::ApiError_InvalidJsonFormat);
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k200OK);
        co_return resp;
    }

    PostParamMap pm;
    pm.addParam("username", false)
      .addParam("nickname", false)
      .addParam("email", false)
      .addParam("tel", false)
      .addParam("newMfaCode", false)
      .addParam("unbindEmail", false)
      .addParam("unbindPhone", false)
      .addParam("isMale", false)
      .addParam("user_password", false);

    pm.readParamsFromJson(*reqJson);
    auto missings = pm.checkRequiredParams();

    if(pm.hasParam("email") && pm.hasParam("tel")){
        missings.push_back("email和tel不可同时修改");
    }

    // 检查解绑参数互斥
    if(pm.hasParam("unbindEmail") && pm.hasParam("email")) {
        missings.push_back("不可同时修改和解绑邮箱");
    }
    if(pm.hasParam("unbindPhone") && pm.hasParam("tel")) {
        missings.push_back("不可同时修改和解绑电话");
    }

    // 修改邮箱或电话需要验证码, 解绑也需要原邮箱或电话的验证码
    if((pm.hasParam("email") || pm.hasParam("tel") || pm.hasParam("unbindEmail") || pm.hasParam("unbindPhone")) && !pm.hasParam("newMfaCode")){
        missings.push_back("修改/解绑email或tel需要提供验证码");
    }

    if (!missings.empty()) {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "缺少必要参数" + std::accumulate(missings.begin(), missings.end(), std::string(""), 
            [](const std::string& a, const std::string& b) { return a + ", " + b; }));
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k200OK);
        co_return resp;
    }

    int userId = -1;
    auto attributes = req->getAttributes();
    if (attributes->find("userId")) {
        userId = attributes->get<int>("userId");
    }
    result = co_await _authService->UpdateUserInfo(userId, pm);
    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    co_return resp;
}

Task<HttpResponsePtr> UserController::deleteUser(HttpRequestPtr req) {
    auto _authService = AuthService::Instance();
    HttpResult result;
    int userId = -1;
    auto attributes = req->getAttributes();
    if (attributes->find("userId")) {
        userId = attributes->get<int>("userId");
    }
    
    // 因为已经通过了 ActionTokenFilter 校验，不再需要传入 mfaCode 和 target 再次验证
    // 所以我们调用原本的 DeleteUserForce 直接进行删除
    result = co_await _authService->DeleteUserForce(userId);
    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    co_return resp;
}

Task<HttpResponsePtr> UserController::generateActionToken(HttpRequestPtr req, std::string mfaTypeStr, std::string mfaCode, std::string target) {
    auto _actionTokenService = ActionTokenService::Instance();
    
    int userId = -1;
    auto attributes = req->getAttributes();
    if (attributes->find("userId")) {
        userId = attributes->get<int>("userId");
    }

    HttpResult result = co_await _actionTokenService->GenerateTokenForUser(userId, mfaTypeStr, mfaCode, target);

    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    co_return resp;
}

Task<HttpResponsePtr> UserController::generateActionTokenBeforeLogin(HttpRequestPtr req, std::string mfaTypeStr, std::string mfaCode, std::string target) {
    auto _actionTokenService = ActionTokenService::Instance();

    HttpResult result = co_await _actionTokenService->GenerateAnonymousToken(mfaTypeStr, mfaCode, target);

    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    co_return resp;
}

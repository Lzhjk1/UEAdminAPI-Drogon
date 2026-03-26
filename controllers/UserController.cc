#include "UserController.h"
#include "services/ThirdPartyLoginService.h"
#include "services/AuthService.h"
#include <utils/PostParamMap.h>
#include <utils/DataFormatUtils.h>
#include <utils/HttpResult.h>
#include <numeric>

using namespace drogon;
using namespace UEAdminAPI;
using namespace UEAdminAPI::utils;
using namespace UEAdminAPI::Services;


Task<HttpResponsePtr> UserController::updateUser(HttpRequestPtr req) {
    auto _authService = AuthService::Instance();

    auto resp = HttpResponse::newHttpResponse();
    HttpResult result;

    auto reqJson = req->getJsonObject();
    if (!reqJson) {
        result.setResult(ApiErrorCode::ApiError_InvalidJsonFormat);
        resp->setBody(result.toJsonString());
        resp->setStatusCode(k200OK);
        co_return resp;
    }

    PostParamMap pm;
    // mfaCode 和 target 用于 MFA 验证 (修改敏感信息时需要)
    // 特殊情况: 如果用户未绑定邮箱和电话(如第三方登录创建的初始账号), 则可以直接绑定, 此时不需要这两个参数
    pm.addParam("mfaCode", false)
      .addParam("target", false)
      .addParam("username", false)
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
        resp->setBody(result.toJsonString());
        resp->setStatusCode(k200OK);
        co_return resp;
    }

    int userId = req->getAttributes()->get<int>("userId");
    result = co_await _authService->UpdateUserInfo(userId, pm);
    resp->setBody(result.toJsonString());
    resp->setStatusCode(k200OK);
    co_return resp;
}

Task<HttpResponsePtr> UserController::deleteUser(HttpRequestPtr req, const std::string mfaCode, const std::string target) {
    auto _authService = AuthService::Instance();

    auto resp = HttpResponse::newHttpResponse();
    HttpResult result;
    int userId = req->getAttributes()->get<int>("userId");
    result = co_await _authService->DeleteUser(userId, target, mfaCode);
    resp->setBody(result.toJsonString());
    resp->setStatusCode(k200OK);
    co_return resp;
}

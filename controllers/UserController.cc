#include "UserController.h"
#include "services/ThirdPartyLoginService.h"
#include "services/AuthService.h"
#include <utils/PostParamMap.h>
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
        result.setResult(-1, "请求体必须是JSON格式");
        resp->setBody(result.toJsonString());
        resp->setStatusCode(k400BadRequest);
        co_return resp;
    }

    PostParamMap pm;
    pm.addParam("verifyCode", true)
      .addParam("target", true)
      .addParam("username", false)
      .addParam("nickname", false)
      .addParam("email", false)
      .addParam("tel", false)
      .addParam("newEmailOrPhoneVerifyCode", false)
      .addParam("isMale", false)
      .addParam("user_password", false);

    pm.readParamsFromJson(*reqJson);
    auto missings = pm.checkRequiredParams();

    auto token = req->getHeader("token");
    if(token.empty()){
        missings.push_back("token in header");
    }

    if(pm.hasParam("email") && pm.hasParam("tel")){
        missings.push_back("email和tel不可同时修改");
    }

    if((pm.hasParam("email") || pm.hasParam("tel")) && !pm.hasParam("newEmailOrPhoneVerifyCode")){
        missings.push_back("修改email或tel需要提供新邮箱或电话的验证码");
    }

    if (!missings.empty()) {
        result.setResult(-1, "缺少必要参数" + std::accumulate(missings.begin(), missings.end(), std::string(""), 
            [](const std::string& a, const std::string& b) { return a + ", " + b; }));
        resp->setBody(result.toJsonString());
        resp->setStatusCode(k400BadRequest);
        co_return resp;
    }

    result = co_await _authService->UpdateUserInfo(token, pm);
    resp->setBody(result.toJsonString());
    resp->setStatusCode(result.code == 0 ? k200OK : k400BadRequest);
    co_return resp;
}

Task<HttpResponsePtr> UserController::deleteUser(HttpRequestPtr req, const std::string verifyCode, const std::string target) {
    auto _authService = AuthService::Instance();

    auto resp = HttpResponse::newHttpResponse();
    HttpResult result;
    auto token = req->getHeader("token");
    result = co_await _authService->DeleteUser(token, target, verifyCode);
    resp->setBody(result.toJsonString());
    resp->setStatusCode(result.code == 0 ? k200OK : k400BadRequest);
    co_return resp;
}

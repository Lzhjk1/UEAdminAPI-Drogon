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
        resp->setBody(result.toJsonString());
        resp->setStatusCode(k200OK);
        co_return resp;
    }

    int userId = -1;
    auto attributes = req->getAttributes();
    if (attributes->find("userId")) {
        userId = attributes->get<int>("userId");
    }
    result = co_await _authService->UpdateUserInfo(userId, pm);
    resp->setBody(result.toJsonString());
    resp->setStatusCode(k200OK);
    co_return resp;
}

Task<HttpResponsePtr> UserController::deleteUser(HttpRequestPtr req) {
    auto _authService = AuthService::Instance();

    auto resp = HttpResponse::newHttpResponse();
    HttpResult result;
    int userId = -1;
    auto attributes = req->getAttributes();
    if (attributes->find("userId")) {
        userId = attributes->get<int>("userId");
    }
    
    // 因为已经通过了 ActionTokenFilter 校验，不再需要传入 mfaCode 和 target 再次验证
    // 所以我们调用原本的 DeleteUserForce 直接进行删除
    result = co_await _authService->DeleteUserForce(userId);
    resp->setBody(result.toJsonString());
    resp->setStatusCode(k200OK);
    co_return resp;
}

Task<HttpResponsePtr> UserController::generateActionToken(HttpRequestPtr req, std::string mfaTypeStr, std::string mfaCode, std::string target) {
    auto _authService = AuthService::Instance();
    auto _actionTokenService = ActionTokenService::Instance();
    auto _mfaService = MFAService::Instance();
    
    auto resp = HttpResponse::newHttpResponse();
    HttpResult result;

    if (mfaTypeStr.empty()) {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "缺少参数: mfaType");
        resp->setBody(result.toJsonString());
        resp->setStatusCode(k200OK);
        co_return resp;
    }

    int userId = -1;
    auto attributes = req->getAttributes();
    if (attributes->find("userId")) {
        userId = attributes->get<int>("userId");
    }
    eMFAType mfaType = stringToMFAType(mfaTypeStr);

    if (mfaType == eMFAType::Error) {
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "未知的操作类别 (mfaType)");
        resp->setBody(result.toJsonString());
        resp->setStatusCode(k200OK);
        co_return resp;
    }

    // 检查用户是否绑定了邮箱或手机号
    auto dbClientPtr = drogon::app().getDbClient();
    drogon::orm::Mapper<drogon_model::UEAdminAPI::User> mapperUser(dbClientPtr);
    drogon_model::UEAdminAPI::User user;

    if (userId > 0) {
        try {
            user = mapperUser.findByPrimaryKey(userId);
        } catch (...) {
            result.setResult(ApiErrorCode::ApiError_UserNotFound);
            resp->setBody(result.toJsonString());
            resp->setStatusCode(k200OK);
            co_return resp;
        }

        bool hasEmail = user.getEmail() && !user.getValueOfEmail().empty();
        bool hasPhone = user.getTelephoneNumber() && !user.getValueOfTelephoneNumber().empty();

        if (hasEmail || hasPhone) {
            // 如果绑定了邮箱或手机号，必须进行 MFA 验证
            if (mfaCode.empty() || target.empty()) {
                result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "需要提供 mfaCode 和 target");
                resp->setBody(result.toJsonString());
                resp->setStatusCode(k200OK);
                co_return resp;
            }

            // 验证 MFA
            auto [mfaOk, mfaErr] = co_await _authService->VerifyUserTargetMFA(target, mfaCode, user, mfaType);
            if (!mfaOk) {
                result.setResult(ApiErrorCode::ApiError_InvalidVerifyCode, mfaErr.empty() ? std::string("验证码错误") : mfaErr);
                resp->setBody(result.toJsonString());
                resp->setStatusCode(k200OK);
                co_return resp;
            }
        }
    } else {
        // 未登录状态，只需验证验证码是否正确，无需校验归属
        if (mfaCode.empty() || target.empty()) {
            result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "需要提供 mfaCode 和 target");
            resp->setBody(result.toJsonString());
            resp->setStatusCode(k200OK);
            co_return resp;
        }

        auto [isSuccess, errMsg] = co_await _mfaService->VerifyTheCode(target, mfaCode, mfaType);
        if (!isSuccess) {
            result.setResult(ApiErrorCode::ApiError_InvalidVerifyCode, errMsg.empty() ? std::string("验证码错误") : errMsg);
            resp->setBody(result.toJsonString());
            resp->setStatusCode(k200OK);
            co_return resp;
        }
    }

    // 验证成功 (或者无需验证)，颁发 ActionToken
    std::string token = _actionTokenService->GenerateToken(userId, mfaType, 300); // 5分钟有效期

    result.setResult(ApiErrorCode::ApiError_Success, "ActionToken 颁发成功");
    result.jsondata["actionToken"] = token;
    result.jsondata["mfaType"] = mfaTypeStr;
    result.jsondata["expiresIn"] = 300;

    resp->setBody(result.toJsonString());
    resp->setStatusCode(k200OK);
    co_return resp;
}

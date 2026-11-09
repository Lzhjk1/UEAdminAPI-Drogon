#include "ThirdPartyLogin.h"

#include "models/User.h"
#include "models/ThirdPartyPlatforms.h"
#include "models/UserThirdPartyInfo.h"

#include "services/ThirdPartyLoginService.h"
#include "services/MFAService.h"
#include "services/AuthService.h"

#include <json/json.h>
#include <utils/PostParamMap.h>
#include <utils/HttpResult.h>
#include <iostream>
#include <numeric>
#include <drogon/orm/Mapper.h>

#include "utils/RandomGenerator.h"
#include "utils/DataFormatUtils.h"

using namespace drogon;
using namespace drogon::orm;
using namespace drogon_model::UEAdminAPI;
using namespace UEAdminAPI::Services;
using namespace UEAdminAPI::utils;

namespace UEAdminAPI {
namespace Controllers {

namespace {
std::string ReadJsonOrQuery(const HttpRequestPtr &req, const char *key) {
    auto json = req->getJsonObject();
    if (json && json->isMember(key) && (*json)[key].isString()) {
        return (*json)[key].asString();
    }
    auto param = req->getParameter(key);
    if (!param.empty()) {
        return param;
    }
    if (json && json->isMember(key)) {
        const auto &node = (*json)[key];
        if (node.isBool()) {
            return node.asBool() ? "true" : "false";
        }
        if (node.isInt()) {
            return std::to_string(node.asInt());
        }
    }
    return "";
}

bool ReadBoolJsonOrQuery(const HttpRequestPtr &req, const char *key) {
    const std::string value = ReadJsonOrQuery(req, key);
    return value == "true" || value == "1";
}
} // namespace

Task<HttpResponsePtr> ThirdPartyLogin::getLoginUrl(HttpRequestPtr req) {
    const std::string platform = req->getParameter("platform");
    std::string userCode = req->getParameter("user_code");
    if (userCode.empty()) {
        userCode = req->getParameter("ueadmin_state");
    }
    auto _thirdPartyLoginService = ThirdPartyLoginService::Instance();
    auto result = co_await _thirdPartyLoginService->GetLoginUrl(platform, userCode);
    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    co_return resp;
}

Task<HttpResponsePtr> ThirdPartyLogin::callback(HttpRequestPtr req,
                              const std::string platform,
                              const std::string code,
                              const std::string state) {
    auto _thirdPartyLoginService = ThirdPartyLoginService::Instance();
    co_return co_await _thirdPartyLoginService->CallbackRedirect(platform, code, state);
}

Task<HttpResponsePtr> ThirdPartyLogin::bindAccount(HttpRequestPtr req,
                                    const std::string platform,
                                    const std::string code,
                                    const std::string verifyCode) {
    auto _thirdPartyLoginService = ThirdPartyLoginService::Instance();
    int userId = req->getAttributes()->get<int>("userId");
    auto result = co_await _thirdPartyLoginService->BindAccount(userId, platform, code, verifyCode);
    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    co_return resp;
}

Task<HttpResponsePtr> ThirdPartyLogin::verifyLogin(HttpRequestPtr req,
                                                const std::string platform,
                                                const std::string code,
                                                const std::string verifyCode, 
                                                bool onlyCheck) {
    auto _thirdPartyLoginService = ThirdPartyLoginService::Instance();
    auto result = co_await _thirdPartyLoginService->VerifyLogin(platform, code, verifyCode, onlyCheck);   
    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    co_return resp;
}

Task<HttpResponsePtr> ThirdPartyLogin::loginWithThirdParty(HttpRequestPtr req,
                                                const std::string platform,
                                                const std::string code,
                                                const std::string verifyCode) {
    auto _thirdPartyLoginService = ThirdPartyLoginService::Instance();
    auto result = co_await _thirdPartyLoginService->LoginWithThirdParty(platform, code, verifyCode);
    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    co_return resp;
}

Task<HttpResponsePtr> ThirdPartyLogin::createUserFromThirdParty(HttpRequestPtr req, const std::string platform, const std::string code, const std::string verifyCode) {
    auto _thirdPartyLoginService = ThirdPartyLoginService::Instance();
    auto result = co_await _thirdPartyLoginService->CreateUserFromThirdParty(platform, code, verifyCode);
    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    co_return resp;
}

Task<HttpResponsePtr> ThirdPartyLogin::verifyLoginPost(HttpRequestPtr req) {
    co_return co_await verifyLogin(req,
                                   ReadJsonOrQuery(req, "platform"),
                                   ReadJsonOrQuery(req, "code"),
                                   ReadJsonOrQuery(req, "verifyCode"),
                                   ReadBoolJsonOrQuery(req, "onlyCheck"));
}

Task<HttpResponsePtr> ThirdPartyLogin::loginWithThirdPartyPost(HttpRequestPtr req) {
    co_return co_await loginWithThirdParty(req,
                                          ReadJsonOrQuery(req, "platform"),
                                          ReadJsonOrQuery(req, "code"),
                                          ReadJsonOrQuery(req, "verifyCode"));
}

Task<HttpResponsePtr> ThirdPartyLogin::bindAccountPost(HttpRequestPtr req) {
    co_return co_await bindAccount(req,
                                   ReadJsonOrQuery(req, "platform"),
                                   ReadJsonOrQuery(req, "code"),
                                   ReadJsonOrQuery(req, "verifyCode"));
}

Task<HttpResponsePtr> ThirdPartyLogin::createUserFromThirdPartyPost(HttpRequestPtr req) {
    co_return co_await createUserFromThirdParty(req,
                                                ReadJsonOrQuery(req, "platform"),
                                                ReadJsonOrQuery(req, "code"),
                                                ReadJsonOrQuery(req, "verifyCode"));
}

Task<HttpResponsePtr> ThirdPartyLogin::unbindAccount(HttpRequestPtr req,
                                                    const std::string platform) {
    auto _thirdPartyLoginService = ThirdPartyLoginService::Instance();
    HttpResult result;
    int userId = req->getAttributes()->get<int>("userId");
    result = co_await _thirdPartyLoginService->UnbindAccount(userId, platform);
    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    co_return resp;
}

} // namespace Controllers
} // namespace UEAdminAPI

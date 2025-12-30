#include "ThirdPartyLogin.h"

#include "models/User.h"
#include "models/ThirdpartyPlatforms.h"
#include "models/UserThirdpartyInfo.h"

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

Task<HttpResponsePtr> ThirdPartyLogin::getLoginUrl(HttpRequestPtr req,
                                 const std::string platform) {
    auto _thirdPartyLoginService = ThirdPartyLoginService::Instance();
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    auto result = co_await _thirdPartyLoginService->GetLoginUrl(platform);
    resp->setBody(result.toJsonString());
    co_return resp;
}

Task<HttpResponsePtr> ThirdPartyLogin::callback(HttpRequestPtr req,
                              const std::string platform,
                              const std::string code,
                              const std::string state) {
    auto _thirdPartyLoginService = ThirdPartyLoginService::Instance();
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    auto result = co_await _thirdPartyLoginService->Callback(platform, code, state);
    resp->setBody(result.toJsonString());
    co_return resp;
}

Task<HttpResponsePtr> ThirdPartyLogin::bindAccount(HttpRequestPtr req,
                                    const std::string platform,
                                    const std::string code,
                                    const std::string verifyCode) {
    auto _thirdPartyLoginService = ThirdPartyLoginService::Instance();
    auto resp = HttpResponse::newHttpResponse();
    auto [authType, token] = UEAdminAPI::DataFormatUtil::parseTokenFromAuthorizationHeader(req->getHeader("Authorization"));
    auto result = co_await _thirdPartyLoginService->BindAccount(token, platform, code, verifyCode);
    resp->setBody(result.toJsonString());
    co_return resp;
}

Task<HttpResponsePtr> ThirdPartyLogin::verifyLogin(HttpRequestPtr req,
                                                const std::string platform,
                                                const std::string code,
                                                const std::string verifyCode, 
                                                bool onlyCheck) {
    auto _thirdPartyLoginService = ThirdPartyLoginService::Instance();
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    auto result = co_await _thirdPartyLoginService->VerifyLogin(platform, code, verifyCode, onlyCheck);
    resp->setBody(result.toJsonString());
    co_return resp;
}

Task<HttpResponsePtr> ThirdPartyLogin::loginWithThirdParty(HttpRequestPtr req,
                                                const std::string platform,
                                                const std::string code,
                                                const std::string verifyCode) {
    auto _thirdPartyLoginService = ThirdPartyLoginService::Instance();
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    auto result = co_await _thirdPartyLoginService->LoginWithThirdParty(platform, code, verifyCode);
    resp->setBody(result.toJsonString());
    co_return resp;
}

Task<HttpResponsePtr> ThirdPartyLogin::createUserFromThirdParty(HttpRequestPtr req, const std::string platform, const std::string code, const std::string verifyCode) {
    auto _thirdPartyLoginService = ThirdPartyLoginService::Instance();
    auto resp = HttpResponse::newHttpResponse();
    auto result = co_await _thirdPartyLoginService->CreateUserFromThirdPartyAndLogin(platform, code, verifyCode);
    resp->setBody(result.toJsonString());
    co_return resp;
}

Task<HttpResponsePtr> ThirdPartyLogin::unbindAccount(HttpRequestPtr req, const std::string platform, const std::string mfaTarget, const std::string verifyCode) {
    auto _thirdPartyLoginService = ThirdPartyLoginService::Instance();
    auto resp = HttpResponse::newHttpResponse();
    HttpResult result;
    auto [authType2, token] = UEAdminAPI::DataFormatUtil::parseTokenFromAuthorizationHeader(req->getHeader("Authorization"));
    result = co_await _thirdPartyLoginService->UnbindAccount(token, platform, mfaTarget, verifyCode);
    resp->setBody(result.toJsonString());
    resp->setStatusCode(result.code == 0 ? k200OK : k400BadRequest);
    co_return resp;
}

} // namespace Controllers
} // namespace UEAdminAPI

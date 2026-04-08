#include "Login.h"
#include "utils/HttpResult.h"
#include "utils/DataFormatUtils.h"

#include "services/AuthService.h"

#include "models/User.h"
using namespace drogon_model::UEAdminAPI;

using namespace UEAdminAPI;
using namespace UEAdminAPI::utils;

// Add definition of your processing function here

Task<HttpResponsePtr> Login::LoginByPwd(HttpRequestPtr req, std::string userName, std::string pwd) 
{
    // 依赖
    auto _authService = AuthService::Instance();

	auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);

    HttpResult result = co_await _authService->LoginByPwd(userName, pwd);

    resp->setBody(result.toJsonString());
    resp->setStatusCode(k200OK);

	co_return resp;
}

Task<HttpResponsePtr> Login::LoginByOther(HttpRequestPtr req, std::string target, std::string targetDBColName) {
    auto _authService = AuthService::Instance();
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);

    HttpResult result;
    if (target.empty()) {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "缺少参数 target");
        resp->setBody(result.toJsonString());
        co_return resp;
    }
    result = co_await _authService->LoginByOther(target, targetDBColName);
    resp->setBody(result.toJsonString());
    co_return resp;
}

Task<HttpResponsePtr> Login::Logout(HttpRequestPtr req) {
    auto _authService = AuthService::Instance();

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);

    int userId = req->getAttributes()->get<int>("userId");
    HttpResult result = co_await _authService->Logout(userId);

    resp->setBody(result.toJsonString());
    resp->setStatusCode(k200OK);

    co_return resp;
}

Task<HttpResponsePtr> Login::LoginByEmail(HttpRequestPtr req, std::string email){
    co_return co_await LoginByOther(req, email, User::Cols::_email);
}

Task<HttpResponsePtr> Login::LoginByPhone(HttpRequestPtr req, std::string phone) {
    co_return co_await LoginByOther(req, phone, User::Cols::_telephone_number);
}

Task<HttpResponsePtr> Login::LoginByFlashToken(HttpRequestPtr req) {
    auto _authService = AuthService::Instance();

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);

    auto [authType, token] = UEAdminAPI::DataFormatUtil::parseTokenFromAuthorizationHeader(req->getHeader("AuthorizationFlashToken"));
    HttpResult result = co_await _authService->LoginByFlashToken(token);

    resp->setBody(result.toJsonString());
    resp->setStatusCode(k200OK);

    co_return resp;
}

Task<HttpResponsePtr> Login::VerifyFlashToken(HttpRequestPtr req, std::string token) {
    auto _authService = AuthService::Instance();
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);

    HttpResult result = co_await _authService->VerifyToken(token);

    if (result.code == 0 && result.jsondata["tokenType"].asString() != "flashToken") {
        result.setResult(ApiErrorCode::ApiError_FlashTokenVerificationFailed, "不是有效的 FlashToken");
    }

    resp->setBody(result.toJsonString());
    co_return resp;
}

Task<HttpResponsePtr> Login::VerifyAuthToken(HttpRequestPtr req, std::string token) {
    auto _authService = AuthService::Instance();
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);

    HttpResult result = co_await _authService->VerifyToken(token);

    if (result.code == 0 && result.jsondata["tokenType"].asString() != "token") {
        result.setResult(ApiErrorCode::ApiError_TokenInvalidOrExpired, "不是有效的 Token");
    }

    resp->setBody(result.toJsonString());
    co_return resp;
}

Task<HttpResponsePtr> Login::GetSelfInfo(HttpRequestPtr req) {
    auto _authService = AuthService::Instance();

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);

    int userId = req->getAttributes()->get<int>("userId");
    HttpResult result = co_await _authService->GetSelfInfo(userId);

    resp->setBody(result.toJsonString());
    resp->setStatusCode(k200OK);

    co_return resp;
}

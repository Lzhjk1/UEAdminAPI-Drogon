#include "Login.h"
#include "utils/HttpResult.h"
#include "utils/DataFormatUtils.h"

#include "services/AuthService.h"


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

Task<HttpResponsePtr> Login::LoginByOther(HttpRequestPtr req, std::string target, std::string targetDBColName, std::string mfaCode) {
    // 依赖
    auto _authService = AuthService::Instance();

    // 用于返回结果的相关变量预定义
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);

    HttpResult result = co_await _authService->LoginByOther(target, targetDBColName, mfaCode);

    resp->setBody(result.toJsonString());
    resp->setStatusCode(k200OK);

    co_return resp;
}

Task<HttpResponsePtr> Login::Logout(HttpRequestPtr req) {
    auto _authService = AuthService::Instance();

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);

    auto [authType, token] = UEAdminAPI::DataFormatUtil::parseTokenFromAuthorizationHeader(req->getHeader("Authorization"));
    HttpResult result = co_await _authService->Logout(token);

    resp->setBody(result.toJsonString());
    resp->setStatusCode(k200OK);

    co_return resp;
}

Task<HttpResponsePtr> Login::LoginByEmail(HttpRequestPtr req, std::string email, std::string mfaCode){
    // 依赖
    auto _authService = AuthService::Instance();
    
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    
    HttpResult result = co_await _authService->LoginByEmail(email, mfaCode);
    
    resp->setBody(result.toJsonString());
    resp->setStatusCode(k200OK);
    
    co_return resp;
}

Task<HttpResponsePtr> Login::LoginByPhone(HttpRequestPtr req, std::string phone, std::string mfaCode) {
    // 依赖
    auto _authService = AuthService::Instance();
    
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    
    HttpResult result = co_await _authService->LoginByPhone(phone, mfaCode);
    
    resp->setBody(result.toJsonString());
    resp->setStatusCode(k200OK);
    
    co_return resp;
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

Task<HttpResponsePtr> Login::VerifyToken(HttpRequestPtr req) {
    auto _authService = AuthService::Instance();

    auto resp = HttpResponse::newHttpResponse();
    auto [authType, token] = UEAdminAPI::DataFormatUtil::parseTokenFromAuthorizationHeader(req->getHeader("Authorization"));
    HttpResult result = co_await _authService->VerifyToken(token);
    resp->setBody(result.toJsonString());
    resp->setStatusCode(k200OK);
    co_return resp;
}

Task<HttpResponsePtr> Login::GetSelfInfo(HttpRequestPtr req) {
    auto _authService = AuthService::Instance();

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);

    auto [authType, token] = UEAdminAPI::DataFormatUtil::parseTokenFromAuthorizationHeader(req->getHeader("Authorization"));
    HttpResult result = co_await _authService->GetSelfInfo(token);

    resp->setBody(result.toJsonString());
    resp->setStatusCode(k200OK);

    co_return resp;
}

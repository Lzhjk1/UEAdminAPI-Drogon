#include "Login.h"
#include "utils/HttpResult.h"
#include "services/AuthService.h"

using namespace UEAdminAPI;
using namespace UEAdminAPI::utils;
// Add definition of your processing function here

Task<HttpResponsePtr> Login::LoginByPwd(HttpRequestPtr req, std::string userName, std::string pwd) 
{
    // 依赖
    auto _authService = AuthService::Instance();

	auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k400BadRequest);

    HttpResult result = co_await _authService->LoginByPwd(userName, pwd);

    resp->setBody(result.toJsonString());
    resp->setStatusCode(result.code == 0 ? k200OK : k400BadRequest);

	co_return resp;
}

Task<HttpResponsePtr> Login::LoginByOther(HttpRequestPtr req, std::string target, std::string targetDBColName, std::string code) {
    // 依赖
    auto _authService = AuthService::Instance();

    // 用于返回结果的相关变量预定义
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);

    HttpResult result = co_await _authService->LoginByOther(target, targetDBColName, code);

    resp->setBody(result.toJsonString());
    resp->setStatusCode(result.code == 0 ? k200OK : k500InternalServerError);

    co_return resp;
}

Task<HttpResponsePtr> Login::LoginByEmail(HttpRequestPtr req, std::string email, std::string code){
    // 依赖
    auto _authService = AuthService::Instance();
    
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    
    HttpResult result = co_await _authService->LoginByEmail(email, code);
    
    resp->setBody(result.toJsonString());
    resp->setStatusCode(result.code == 0 ? k200OK : k500InternalServerError);
    
    co_return resp;
}

Task<HttpResponsePtr> Login::LoginByPhone(HttpRequestPtr req, std::string phone, std::string code) {
    // 依赖
    auto _authService = AuthService::Instance();
    
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    
    HttpResult result = co_await _authService->LoginByPhone(phone, code);
    
    resp->setBody(result.toJsonString());
    resp->setStatusCode(result.code == 0 ? k200OK : k500InternalServerError);
    
    co_return resp;
}

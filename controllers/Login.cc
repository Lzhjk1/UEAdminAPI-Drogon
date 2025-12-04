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

Task<HttpResponsePtr> Login::VerifyToken(HttpRequestPtr req, std::string token) {
    // 依赖
    auto _authService = AuthService::Instance();
    
    auto resp = HttpResponse::newHttpResponse();\
    auto result = HttpResult();

    // 检查参数
    if(!DataFormatUtil::isJwtString(token)){
        result.setResult(1, "invalid token");
        resp->setBody(result.toJsonString());
        resp->setStatusCode(k400BadRequest);
        co_return resp;
    }
    
    auto [isSuccess, userId, status, isFlashToken] = _authService->CheckTokenAndParseUserId(token);
    // 如果tokenType为-1, 则说明token失效过期, 仍会正常获取tokenType
    result.jsondata["userId"] = userId;
    result.jsondata["status"] = status;
    result.jsondata["tokenType"] = isFlashToken == -1 ? "unset" : isFlashToken == 1 ? "flashToken" : "token";

    if(!isSuccess){
        result.setResult(-1, "Token验证失败");
        resp->setBody(result.toJsonString());
        resp->setStatusCode(k400BadRequest);
        co_return resp;
    }

    result.setResult(0, "Token验证成功");
    resp->setBody(result.toJsonString());
    
    co_return resp;
}

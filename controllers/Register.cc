#include "Register.h"
#include "models/User.h"
#include "models/UserGitlabInfo.h"
#include <utils/EnumUserPrivileges.h>
#include "services/AuthService.h"
#include "services/MFAService.h"
#include "services/GitlabService.h"
#include <utils/PostParamMap.h>
#include <utils/HttpResult.h>
#include <numeric>

using namespace std;
using namespace drogon;
using namespace drogon::orm;
using namespace drogon_model::UEAdminAPI;
using namespace UEAdminAPI;
using namespace UEAdminAPI::utils;

Task<HttpResponsePtr> Register::RegisterUser(HttpRequestPtr req) {
    // 依赖
    auto _authService = AuthService::Instance();
    auto _mfaService = MFAService::Instance();
    auto _gitlabService = GitlabService::Instance();

	auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    HttpResult result;

	auto reqJson = req->getJsonObject();
    if (!reqJson) {
        result.setResult(ApiErrorCode::ApiError_InvalidJsonFormat);
        resp->setBody(result.toJsonString());
        resp->setStatusCode(k200OK);
        co_return resp;
    }

    // 使用PostParamMap类处理参数
    PostParamMap paramMap;
    // 设置需要的参数
    paramMap.addParam("username", true)
        .addParam("user_password", true)
        .addParam("privilege", false, "User")
        .addParam("nickname", false)
        .addParam("email", false)
        .addParam("tel", false)
        .addParam("isMale", false, "true")
        .addParam("verifyCode", true)
        // 同时第三方平台注册
        .addParam("third_platform_name", false)
        .addParam("third_code", false)
        .addParam("third_verifyCode", false);

    paramMap.readParamsFromJson(*reqJson);
    std::vector<std::string> missingFields = paramMap.checkRequiredParams();

    // 通过检测email和phone字段来判断注册方式
    bool isEmailRegister = false;
    if(!(paramMap.hasParam("email") || paramMap.hasParam("phone"))) {
        missingFields.push_back("email or phone");
    }
    else if(paramMap.hasParam("email")) {
        isEmailRegister = true;
    }

    // 检测第三方平台注册参数是否设置
    // 如果平台被指定, 则假定要同时注册第三方平台
    // 那么另外的code和verifyCode也要同时给出
    bool isWithThirdPlatform = false;
    if(!paramMap.getParam("third_platform_name").empty()) {
        isWithThirdPlatform = true;
        if(paramMap.getParam("third_code").empty()) {
           missingFields.push_back("third_code");
        }
        if(paramMap.getParam("third_verifyCode").empty()) {
           missingFields.push_back("third_verifyCode");
        }
    }

    // 如果有缺失的必填项，返回错误
    if (!missingFields.empty()) {
        HttpResult result;
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "缺少必填项: " + std::accumulate(missingFields.begin(), missingFields.end(), std::string(), [](const std::string& a, const std::string& b) { return a + ", " + b; }));
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    UserPrivileges privileges;
    try{
        privileges = stringToUserPrivileges(paramMap.getParam("privilege"));
    }
    catch(const std::invalid_argument& e){
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, e.what());
        resp->setBody(result.toJsonString());
        resp->setStatusCode(k200OK);
        co_return resp;
    }

    if(isEmailRegister) {
        if(isWithThirdPlatform){
            result = co_await _authService->RegisterWithThirdPartyByEmail(
                paramMap.getParam("username"), 
                paramMap.getParam("user_password"), 
                paramMap.getParam("email"), 
                paramMap.getParam("verifyCode"),
                paramMap.getParam("third_platform_name"),
                paramMap.getParam("third_code"),
                paramMap.getParam("third_verifyCode"),
                privileges,
                paramMap.getParam("isMale") == "true",
                paramMap.getParam("nickname")
            );
        }
        else{
            result = co_await _authService->RegisterByEmail(
                paramMap.getParam("username"), 
                paramMap.getParam("user_password"), 
                paramMap.getParam("email"), 
                paramMap.getParam("verifyCode"),
                privileges,
                paramMap.getParam("isMale") == "true",
                paramMap.getParam("nickname")
            );
        }
    }
    else{
        if(isWithThirdPlatform){
            result = co_await _authService->RegisterWithThirdPartyByPhone(
                paramMap.getParam("username"), 
                paramMap.getParam("user_password"), 
                paramMap.getParam("phone"), 
                paramMap.getParam("verifyCode"),
                paramMap.getParam("third_platform_name"),
                paramMap.getParam("third_code"),
                paramMap.getParam("third_verifyCode"),
                privileges,
                paramMap.getParam("isMale") == "true",
                paramMap.getParam("nickname")
            );
        }
        else{
            result = co_await _authService->RegisterByPhone(
                paramMap.getParam("username"),
                paramMap.getParam("user_password"),
                paramMap.getParam("phone"),
                paramMap.getParam("verifyCode"),
                privileges,
                paramMap.getParam("isMale") == "true",
                paramMap.getParam("nickname")   
            );
        }
    }

    resp->setBody(result.toJsonString());

    co_return resp;
}

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

    // 使用PostParamMap类处理参数
    PostParamMap paramMap;
    // 设置需要的参数
    paramMap.addParam("username", true)
        .addParam("password", true)
        .addParam("privilege", false, "User")
        .addParam("nickname", false)
        .addParam("email", false)
        .addParam("phone", false)
        .addParam("isMale", false, "true")
        .addParam("verifyCode", true);

    paramMap.readParamsFromJson(*reqJson);
    std::vector<std::string> missingFields = paramMap.checkRequiredParams();

    // 如果有缺失的必填项，返回错误
    if (!missingFields.empty()) {
        HttpResult result;
        result.setResult(-1, "缺少必填项: " + std::accumulate(missingFields.begin(), missingFields.end(), std::string(), [](const std::string& a, const std::string& b) { return a + ", " + b; }));
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    // 通过检测email和phone字段来判断注册方式
    bool isEmailRegister = false;
    if(!(paramMap.hasParam("email") || paramMap.hasParam("phone"))) {
        missingFields.push_back("email or phone");
    }
    else if(paramMap.hasParam("email")) {
        isEmailRegister = true;
    }

    if(isEmailRegister) {
        result = co_await _authService->RegisterByEmail(
            paramMap.getParam("username"), 
            paramMap.getParam("password"), 
            paramMap.getParam("email"), 
            paramMap.getParam("verifyCode"),
            stringToUserPrivileges(paramMap.getParam("privilege")),
            paramMap.getParam("isMale") == "true",
            paramMap.getParam("nickname")
        );
    }
    else{
        result = co_await _authService->RegisterByPhone(
            paramMap.getParam("username"),
            paramMap.getParam("password"),
            paramMap.getParam("phone"),
            paramMap.getParam("verifyCode"),
            stringToUserPrivileges(paramMap.getParam("privilege")),
            paramMap.getParam("isMale") == "true",
            paramMap.getParam("nickname")   
        );
    }

    resp->setBody(result.toJsonString());

    co_return resp;
}

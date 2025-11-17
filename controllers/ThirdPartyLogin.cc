#include "ThirdPartyLogin.h"
#include "services/ThirdPartyLoginService.h"
#include <json/json.h>
#include <iostream>

using namespace drogon;
using namespace UEAdminAPI::Services;

namespace UEAdminAPI {
namespace Controllers {

Task<HttpResponsePtr> ThirdPartyLogin::getLoginUrl(HttpRequestPtr req,
                                 const std::string platform) const {
    // 获取平台枚举
    auto platformEnum = getPlatformFromString(platform);
    //if (platformEnum == Services::ThirdPartyPlatform::QQ && platform != "QQ") {
    //    auto resp = HttpResponse::newHttpResponse();
    //    resp->setStatusCode(k404NotFound);
    //    resp->setBody("不支持的第三方平台");
    //    co_return resp;
    //}

    // 获取平台服务
    auto platformService = co_await ThirdPartyLoginService::Instance()->getPlatform(platformEnum);
    if (!platformService) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k404NotFound);
        resp->setBody("不支持的第三方平台");
        co_return resp;
    }

    // 创建新的登录值
    auto loginValue = co_await platformService->createNewThirdLoginValue();

    // 获取授权URL
    std::string authUrl = co_await platformService->getAuthorizationUrl(loginValue);

    // 构造返回JSON
    Json::Value response;
    response["code"] = loginValue->code;
    response["verifyCode"] = loginValue->verifyCode;
    response["authorizationUrl"] = authUrl;

    auto resp = HttpResponse::newHttpJsonResponse(response);
    co_return resp;
}

Task<HttpResponsePtr> ThirdPartyLogin::callback(HttpRequestPtr req,
                              const std::string platform,
                              const std::string code,
                              const std::string state) const {
    // 获取平台枚举
    auto platformEnum = getPlatformFromString(platform);

    // 获取平台服务
    auto platformService = co_await ThirdPartyLoginService::Instance()->getPlatform(platformEnum);
    if (!platformService) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k404NotFound);
        resp->setBody("不支持的第三方平台");
        co_return resp;
    }

    // 处理回调
    bool success = co_await platformService->callBack(code, state);

    if (!success) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k400BadRequest);
        resp->setBody("处理第三方登录回调失败");
        co_return resp;
    }

    // 获取登录值
    auto loginValue = co_await platformService->getLoginValue(state);
    if (!loginValue) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k400BadRequest);
        resp->setBody("找不到对应的登录值");
        co_return resp;
    }

    // 构造返回JSON
    Json::Value response;
    response["code"] = loginValue->code;
    response["verifyCode"] = loginValue->verifyCode;
    response["openId"] = loginValue->openId;
    response["nickName"] = loginValue->nickName;
    response["headImgUrl"] = loginValue->headImgUrl;

    auto resp = HttpResponse::newHttpJsonResponse(response);
    co_return resp;
}

Task<HttpResponsePtr> ThirdPartyLogin::verifyLogin(HttpRequestPtr req,
                                                const std::string platform,
                                                const std::string code,
                                                const std::string verifyCode) const {

    // 获取平台枚举
    auto platformEnum = getPlatformFromString(platform);
    if (platformEnum == Services::ThirdPartyPlatform::None) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k404NotFound);
        resp->setBody("不支持的第三方平台");
        co_return resp;
    }

    // 获取平台服务
    auto platformService = co_await ThirdPartyLoginService::Instance()->getPlatform(platformEnum);
    if (!platformService) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k404NotFound);
        resp->setBody("不支持的第三方平台");
        co_return resp;
    }


    if (code.empty() || verifyCode.empty()) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k400BadRequest);
        resp->setBody("缺少必要参数");
        co_return resp;
    }

    // 验证code
    bool success = co_await platformService->verifyCode(code, verifyCode);

    if (!success) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k400BadRequest);
        resp->setBody("验证失败");
        co_return resp;
    }

    // 获取登录值
    auto loginValue = co_await platformService->getLoginValue(code);
    if (!loginValue) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k400BadRequest);
        resp->setBody("找不到对应的登录值");
        co_return resp;
    }

    // 构造返回JSON
    Json::Value response;
    response["success"] = true;
    response["openId"] = loginValue->openId;
    response["nickName"] = loginValue->nickName;
    response["headImgUrl"] = loginValue->headImgUrl;

    auto resp = HttpResponse::newHttpJsonResponse(response);
    co_return resp;
}

Services::ThirdPartyPlatform ThirdPartyLogin::getPlatformFromString(const std::string& platform) const {
    if (platform == "QQ" || platform == "qq") {
        return Services::ThirdPartyPlatform::QQ;
    } else if (platform == "WeChat" || platform == "wechat") {
        return Services::ThirdPartyPlatform::WeChat;
    }

    // 返回一个无效值
    return Services::ThirdPartyPlatform::None;
}

} // namespace Controllers
} // namespace UEAdminAPI

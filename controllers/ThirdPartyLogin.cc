#include "ThirdPartyLogin.h"

#include "models/User.h"
#include "models/ThirdpartyPlatforms.h"
#include "models/UserThirdpartyInfo.h"
#include <drogon/orm/Mapper.h>

#include "services/ThirdPartyLoginService.h"
#include "services/MFAService.h"
#include "services/AuthService.h"

#include <json/json.h>
#include <utils/PostParamMap.h>
#include <utils/HttpResult.h>
#include <iostream>
#include <numeric>

using namespace drogon;
using namespace UEAdminAPI::Services;

namespace UEAdminAPI {
namespace Controllers {

Task<HttpResponsePtr> ThirdPartyLogin::getLoginUrl(HttpRequestPtr req,
                                 const std::string platform) const {
    utils::HttpResult result;
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    
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
        result.setResult(-1, "不支持的第三方平台");
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    // 创建新的登录值
    auto loginValue = co_await platformService->createNewThirdLoginValue();

    // 获取授权URL
    std::string authUrl = co_await platformService->getAuthorizationUrl(loginValue);

    // 构造返回JSON
    result.jsondata["code"] = loginValue->code;
    result.jsondata["verifyCode"] = loginValue->verifyCode;
    result.jsondata["authorizationUrl"] = authUrl;

    resp->setBody(result.toJsonString());
    
    co_return resp;
}

Task<HttpResponsePtr> ThirdPartyLogin::callback(HttpRequestPtr req,
                              const std::string platform,
                              const std::string code,
                              const std::string state) const {
    utils::HttpResult result;
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);

    // 获取平台枚举
    auto platformEnum = getPlatformFromString(platform);

    // 获取平台服务
    auto platformService = co_await ThirdPartyLoginService::Instance()->getPlatform(platformEnum);
    if (!platformService) {
        result.setResult(-1, "不支持的第三方平台");
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    // 处理回调
    bool success = co_await platformService->callBack(code, state);

    if (!success) {
        result.setResult(-1, "处理第三方登录回调失败");
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    // 获取登录值
    auto loginValue = co_await platformService->getLoginValue(state);
    if (!loginValue) {
        result.setResult(-1, "找不到对应的登录值");
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    // 构造返回JSON
    result.jsondata["code"] = loginValue->code;
    result.jsondata["verifyCode"] = loginValue->verifyCode;
    result.jsondata["openId"] = loginValue->openId;
    result.jsondata["nickName"] = loginValue->nickName;
    result.jsondata["headImgUrl"] = loginValue->headImgUrl;

    resp->setBody(result.toJsonString());

    co_return resp;
}

Task<HttpResponsePtr> ThirdPartyLogin::bindAccount(HttpRequestPtr req) {
    throw std::runtime_error("Not Implemented");

    // 梳理一下, 首先需要被绑定账号的信息, 也就是需要身份验证(手机号/邮箱+密码/验证码)\
    // 然后是第三方登录的信息, 也就是需要验证code和verifyCode
    
    // 依赖
    auto _mfaService = MFAService::Instance();
    auto _authService = AuthService::Instance();
    auto _thirdPartyLoginService = ThirdPartyLoginService::Instance();


    auto resp = HttpResponse::newHttpResponse();

    utils::HttpResult result;

    PostParamMap paramMap;
    // 身份验证
    bool isPhone, isPassword;
    paramMap.addParamAsMutex("phone","email")
            .addParamAsMutex("password","verifyCode");
    // 第三方登录
    paramMap.addParam("platform")
            .addParam("code")
            .addParam("verifyCode");
    
    paramMap.readParamsFromJson(req->getJsonObject().get());

    isPhone = paramMap.hasParam("phone");
    isPassword = paramMap.hasParam("password");

    auto missingParams = paramMap.checkRequiredParams();
    
    if (!missingParams.empty()) {
        resp->setStatusCode(k400BadRequest);
        // 组合缺失参数列表作为Body
        result.setResult(-1, "缺少必要参数: " + std::accumulate(missingParams.begin(), missingParams.end(), std::string()));
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    auto dbClientPtr = drogon::app().getDbClient();
    drogon::orm::Mapper<drogon_model::UEAdminAPI::User> mapperUser(dbClientPtr);
    drogon::orm::Mapper<drogon_model::UEAdminAPI::UserThirdPartyInfo> mapperThirdPartyInfo(dbClientPtr);

    if(isPhone && isPassword){
        // 验证手机号和密码
        auto phone = paramMap.getParamValue("phone");
        auto password = paramMap.getParamValue("password");

        auto targetUserFuture = mapperUser.findFutureBy(drogon::orm::Criteria(drogon_model::UEAdminAPI::User::Cols::_telephone_number, drogon::orm::CompareOperator::EQ, phone));
        auto targetUser = targetUserFuture.get();


        if(targetUser.size() >1){
            // 数据库有两个手机号相同的用户, 这不正常
            LOG_ERROR << std::format("有多个个用户使用相同的手机号: {}", targetUser[0].getValueOfTelephoneNumber());
            throw std::runtime_error("There are two users with the same phone number in the database");
        }
        else if (targetUser.size() == 0){
            // 没有找到用户
            result.setResult(-1, "没有找到对应的用户");
            resp->setBody(result.toJsonString());
            co_return resp;
        }

        auto success = _authService->VerifyPasswordHash(password, targetUser[0].getPasswordHash(), targetUser[0].getPasswordSalt());

        if (!success) {
            result.setResult(-1, "账号或密码错误");
            resp->setBody(result.toJsonString());
            co_return resp;
        }

        // 用户验证成功, 之后验证第三方是否已成功callback获取了authorizationCode
        auto platformStr = paramMap.getParamValue("platform");
        auto code = paramMap.getParamValue("code");
        auto verifyCode = paramMap.getParamValue("verifyCode");

        auto platform = co_await _thirdPartyLoginService->getPlatform(getPlatformFromString(platformStr));
        
        platform->verifyCode(code, verifyCode);

        // 绑定账号(将相关信息存入user_third_party_info表)
        auto thirdPartyInfos = targetUser[0].getThird_party_platforms(dbClientPtr);

        // 从查询结果中筛选指定平台
        auto targetInfos = std::find_if(thirdPartyInfos.begin(), thirdPartyInfos.end(), [platform](const std::pair<ThirdPartyPlatforms,UserThirdPartyInfo>& info){
            return info.first.getValueOfPlatformName() == ThirdPartyPlatformToString(platform->getPlatform());
        });


    }

}

Task<HttpResponsePtr> ThirdPartyLogin::verifyLogin(HttpRequestPtr req,
                                                const std::string platform,
                                                const std::string code,
                                                const std::string verifyCode) const {

    throw std::runtime_error("Not Implemented");

    utils::HttpResult result;
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);

    // 获取平台枚举
    auto platformEnum = getPlatformFromString(platform);

    // 获取平台服务
    auto platformService = co_await ThirdPartyLoginService::Instance()->getPlatform(platformEnum);
    if (!platformService) {
        result.setResult(-1, "不支持的第三方平台");
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    if (code.empty() || verifyCode.empty()) {
        result.setResult(-1, "缺少必要参数");
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    // 验证code
    bool success = co_await platformService->verifyCode(code, verifyCode);

    if (!success) {
        result.setResult(-1, "验证失败");
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    // 获取登录值
    auto loginValue = co_await platformService->getLoginValue(code);
    if (!loginValue) {
        result.setResult(-1, "找不到对应的登录值");
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    // 构造返回JSON
    resp->setBody(result.toJsonString());

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

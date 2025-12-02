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

using namespace drogon;
using namespace drogon::orm;
using namespace drogon_model::UEAdminAPI;
using namespace UEAdminAPI::Services;
using namespace UEAdminAPI::utils;

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
    // result.jsondata["code"] = loginValue->code;
    // result.jsondata["verifyCode"] = loginValue->verifyCode;
    // result.jsondata["openId"] = loginValue->openId;
    // result.jsondata["nickName"] = loginValue->nickName;
    // result.jsondata["headImgUrl"] = loginValue->headImgUrl;

    resp->setBody(result.toJsonString());

    co_return resp;
}

Task<HttpResponsePtr> ThirdPartyLogin::bindAccount(HttpRequestPtr req) {
    // 梳理一下, 首先需要被绑定账号的信息, 也就是需要身份验证(手机号/邮箱+密码/验证码)
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
            .addParamAsMutex("password","mfaVerifyCode");
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
    drogon::orm::Mapper<drogon_model::UEAdminAPI::ThirdPartyPlatforms> mapperThirdPartyPlatforms(dbClientPtr);

    // 预先定义, 在之后的四个if块里被赋值     
    User targetUser;

    // 屏蔽这一段
    if (false) {
        if (isPhone && isPassword) {
            // 验证手机号和密码
            auto phone = paramMap.getParam("phone");
            auto password = paramMap.getParam("password");

            // 数据库查找对应用户
            auto foundUsers = mapperUser.findBy(drogon::orm::Criteria(drogon_model::UEAdminAPI::User::Cols::_telephone_number, drogon::orm::CompareOperator::EQ, phone));

            if (foundUsers.size() > 1) {
                // 数据库有两个手机号相同的用户, 这不正常
                LOG_ERROR << std::format("有多个个用户使用相同的手机号: {}", foundUsers[0].getValueOfTelephoneNumber());
                throw std::runtime_error("There are two users with the same phone number in the database");
            } else if (foundUsers.size() == 0) {
                // 没有找到用户
                result.setResult(-1, "没有找到对应的用户");
                resp->setBody(result.toJsonString());
                co_return resp;
            }

            // 验证密码
            auto success = _authService->VerifyPasswordHash(password, foundUsers[0].getPasswordHash(), foundUsers[0].getPasswordSalt());

            if (!success) {
                result.setResult(-1, "账号或密码错误");
                resp->setBody(result.toJsonString());
                co_return resp;
            }

            targetUser = foundUsers[0];
        } else if (isPhone && !isPassword) {
            // 验证手机号和验证码
            auto phone = paramMap.getParam("phone");
            auto verifyCode = paramMap.getParam("mfaVerifyCode");

            // 核对验证码
            auto [isSuccess, errMsg] = co_await _mfaService->VerifyTheCode(phone, verifyCode, eMFAType::ThirdPartyBind);

            if (!isSuccess) {
                result.setResult(-1, errMsg);
                resp->setBody(result.toJsonString());
                co_return resp;
            }

            // 数据库查找对应用户
            auto foundUsers = mapperUser.findBy(drogon::orm::Criteria(drogon_model::UEAdminAPI::User::Cols::_telephone_number, drogon::orm::CompareOperator::EQ, phone));

            if (foundUsers.size() > 1) {
                // 数据库有两个手机号相同的用户, 这不正常
                LOG_ERROR << std::format("有多个个用户使用相同的手机号: {}", foundUsers[0].getValueOfTelephoneNumber());
                throw std::runtime_error("There are two users with the same phone number in the database");
            }

            else if (foundUsers.size() == 0) {
                // 没有找到用户
                result.setResult(-1, "没有找到对应的用户");
                resp->setBody(result.toJsonString());
                co_return resp;
            }

            targetUser = foundUsers[0];
        } else if (!isPhone && isPassword) {
            auto email = paramMap.getParam("email");
            auto password = paramMap.getParam("password");

            // 数据库查找对应用户
            auto foundUsers = mapperUser.findBy(drogon::orm::Criteria(drogon_model::UEAdminAPI::User::Cols::_email, drogon::orm::CompareOperator::EQ, email));

            if (foundUsers.size() > 1) {
                // 数据库有两个邮箱相同的用户, 这不正常
                LOG_ERROR << std::format("有多个个用户使用相同的邮箱: {}", foundUsers[0].getValueOfEmail());
                result.setResult(-1, "内部错误, 请联系管理员");
                resp->setStatusCode(k500InternalServerError);
                resp->setBody(result.toJsonString());
                co_return resp;
            }

            else if (foundUsers.size() == 0) {
                // 没有找到用户
                result.setResult(-1, "没有找到对应的用户");
                resp->setBody(result.toJsonString());
                co_return resp;
            }

            // 验证密码
            auto success = _authService->VerifyPasswordHash(password, foundUsers[0].getPasswordHash(), foundUsers[0].getPasswordSalt());

            if (!success) {
                result.setResult(-1, "账号或密码错误");
                resp->setBody(result.toJsonString());
                co_return resp;
            }

            targetUser = foundUsers[0];
        } else if (!isPhone && !isPassword) {
            // 验证邮箱和验证码
            auto email = paramMap.getParam("email");
            auto verifyCode = paramMap.getParam("mfaVerifyCode");

            // 核对验证码
            auto [isSuccess, errMsg] = co_await _mfaService->VerifyTheCode(email, verifyCode, eMFAType::ThirdPartyBind);

            if (!isSuccess) {
                result.setResult(-1, errMsg);
                resp->setBody(result.toJsonString());
                co_return resp;
            }

            // 数据库查找对应用户
            auto foundUsers = mapperUser.findBy(drogon::orm::Criteria(drogon_model::UEAdminAPI::User::Cols::_email, drogon::orm::CompareOperator::EQ, email));

            if (foundUsers.size() > 1) {
                // 数据库有两个邮箱相同的用户, 这不正常
                LOG_ERROR << std::format("有多个个用户使用相同的邮箱: {}", foundUsers[0].getValueOfEmail());
                result.setResult(-1, "内部错误, 请联系管理员");
                resp->setStatusCode(k500InternalServerError);
                resp->setBody(result.toJsonString());
                co_return resp;
            }

            else if (foundUsers.size() == 0) {
                // 没有找到用户
                result.setResult(-1, "没有找到对应的用户");
                resp->setBody(result.toJsonString());
                co_return resp;
            }

            targetUser = foundUsers[0];
        }
    }

    std::string token = req->getHeader("Authorization");
    auto userId = _authService->CheckTokenAndParseUserId(token);
    if(userId == -1){
        result.setResult(-1, "身份验证失败");
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    try{
        targetUser = mapperUser.findOne(Criteria(User::Cols::_id, CompareOperator::EQ, userId));
    }
    catch (const drogon::orm::DrogonDbException& ex){
        LOG_ERROR << std::format("查找 {} 号用户失败: {}", userId, ex.base().what());
        result.setResult(-1, "内部错误");
        resp->setStatusCode(k500InternalServerError);
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    // 用户验证成功, 之后验证第三方服务是否已成功callback获取了authorizationCode
    auto platformStr = paramMap.getParam("platform");
    auto code = paramMap.getParam("code");
    auto verifyCode = paramMap.getParam("verifyCode");

    auto platform = co_await _thirdPartyLoginService->getPlatform(getPlatformFromString(platformStr));
    
    if(!(co_await platform->verifyCode(code, verifyCode))){
        result.setResult(-1, "验证码错误");
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    // 绑定账号(将相关数据存入user_third_party_info表)
    // 首先获取数据, 检查是否已经绑定过该平台
    auto thirdPartyInfos = targetUser.getThird_party_platforms(dbClientPtr);

    // 从查询结果中筛选指定平台
    auto targetInfos = std::find_if(thirdPartyInfos.begin(), thirdPartyInfos.end(), [platform](const std::pair<ThirdPartyPlatforms,UserThirdPartyInfo>& info){
        return info.first.getValueOfPlatformName() == ThirdPartyPlatformToString(platform->getPlatform());
    });

    if(targetInfos != thirdPartyInfos.end()){
        // 已经绑定过该平台
        result.setResult(-1, "该平台已经绑定过账号");
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    auto targetPlatform = mapperThirdPartyPlatforms.findOne(Criteria(ThirdPartyPlatforms::Cols::_platform_name, drogon::orm::CompareOperator::EQ, platformStr));

    // 没有绑定过该平台, 绑定
    auto loginValue = co_await platform->getLoginValue(code);
    auto thirdPartyInfo = UserThirdPartyInfo();
    thirdPartyInfo.setUserId(targetUser.getValueOfId());
    thirdPartyInfo.setPlatformId(targetPlatform.getValueOfId());
    thirdPartyInfo.setAccessToken(loginValue->accessToken);
    thirdPartyInfo.setNickName(loginValue->nickName);
    thirdPartyInfo.setOpenId(loginValue->openId);
    thirdPartyInfo.setAvatarImgUrl(loginValue->headImgUrl);

    try{
        mapperThirdPartyInfo.insert(thirdPartyInfo);
    }
    catch(const drogon::orm::DrogonDbException& ex){
        LOG_ERROR << std::format("绑定第三方账号失败, 插入第三方信息数据时出现错误: {}", ex.base().what());
        result.setResult(-1, "绑定第三方账号失败");
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    // 绑定成功
    result.setResult(0, "绑定成功");
    resp->setBody(result.toJsonString());
    co_return resp;
}

Task<HttpResponsePtr> ThirdPartyLogin::verifyLogin(HttpRequestPtr req,
                                                const std::string platform,
                                                const std::string code,
                                                const std::string verifyCode) const {

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
        result.setResult(-1, "缺少必要参数, code 和 verifyCode");
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

    if(loginValue->authorizationCode.empty()){
        result.setResult(-1, "该code尚未登录, 验证失败");
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    // 构造返回JSON
    result.setResult(0, "验证成功");
    resp->setBody(result.toJsonString());

    co_return resp;
}

Task<HttpResponsePtr> ThirdPartyLogin::createUserFromThirdParty(HttpRequestPtr req, const std::string platform, const std::string code, const std::string verifyCode) const {
    throw std::runtime_error("Not implemented");
    // 依赖
    auto _authService = AuthService::Instance();

    utils::HttpResult result;
    auto resp = HttpResponse::newHttpResponse();

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
        result.setResult(-1, "缺少必要参数, code 和 verifyCode");
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

    if(loginValue->authorizationCode.empty()){
        result.setResult(-1, "该code尚未登录, 验证失败");
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    std::string username = loginValue->nickName;
    // 生成随机的密码
    std::string password = RandomGenerator::generateRandomPassword();

    // 数据库初始化
    auto dbClientPtr = drogon::app().getDbClient();
    auto mapperUser = drogon::orm::Mapper<User>(dbClientPtr);
    auto mapperThirdPartyInfo = drogon::orm::Mapper<UserThirdPartyInfo>(dbClientPtr);

    // 生成密码hash和salt
    auto [hash, salt] = _authService->CreateStrPasswordHash(password);

    // 创建用户
    User user;
    user.setName(username);
    user.setNickName(username);
    user.setPasswordHash(hash);
    user.setPasswordSalt(salt);
    user.setCreateAt(trantor::Date::now());
    user.setIsMale(true); // 默认
    user.setPrivilege(int(UserPrivileges::User)); // 默认

    

    
    
}

Services::ThirdPartyPlatform ThirdPartyLogin::getPlatformFromString(const std::string& platform) const {
    // 转换为小写
    std::string platformLower = DataFormatUtil::toLowerCase(platform);
    
    if (platform == "qq") {
        return Services::ThirdPartyPlatform::QQ;
    } else if (platform == "wechat") {
        return Services::ThirdPartyPlatform::WeChat;
    }

    // 返回一个无效值
    return Services::ThirdPartyPlatform::None;
}

} // namespace Controllers
} // namespace UEAdminAPI

#include "ThirdPartyLoginService.h"
#include "utils/RandomGenerator.h"
#include <drogon/HttpClient.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>
#include <iostream>
#include <sstream>
#include <algorithm>
#include "utils/DataFormatUtils.h"
#include <shared_mutex>
#include "models/User.h"
#include "models/ThirdpartyPlatforms.h"
#include "models/UserThirdpartyInfo.h"
#include <drogon/orm/Mapper.h>
#include "services/AuthService.h"
#include "services/MFAService.h"
#include "utils/MFA/MFA_Channels.h"
#include "utils/MFA/MFACodePair.h"

using namespace drogon;
using namespace drogon::orm;
using namespace drogon_model::UEAdminAPI;
using namespace UEAdminAPI;
using namespace UEAdminAPI::Services;
using namespace UEAdminAPI::utils;

namespace UEAdminAPI {
namespace Services {

// ThirdPartyLoginValue 实现
ThirdPartyLoginValue::ThirdPartyLoginValue(const std::string& code, const std::string& verifyCode, 
                                          const std::chrono::system_clock::time_point& expireTime)
    : code(code), verifyCode(verifyCode), expireTime(expireTime) {
}

ThirdPartyLoginValue::ThirdPartyLoginValue(const std::string& code, const std::string& verifyCode)
    : code(code), verifyCode(verifyCode), 
      expireTime(std::chrono::system_clock::now() + std::chrono::minutes(2)) {
}

// ThirdPartyLoginPlatformBase 实现
ThirdPartyLoginPlatformBase::ThirdPartyLoginPlatformBase(const Json::Value& config, const UEAdminAPI::utils::EnumThirdPartyPlatform& platform)
    : platform(platform) {
    // 检查并获取配置
    auto [serverHost, clientId, clientSecret] = checkAndGetPlatformConfig(platform, config);
    this->serverHost = serverHost;
    this->clientId = clientId;
    this->clientSecret = clientSecret;

    // 设置重定向URL
    this->redirectUrl = this->serverHost + "/api/third/" + ThirdPartyPlatformToString(platform, true);

    // 创建HTTP客户端
    httpClient = drogon::HttpClient::newHttpClient("https://graph.qq.com");
}

std::tuple<std::string, std::string, std::string> 
ThirdPartyLoginPlatformBase::checkAndGetPlatformConfig(const UEAdminAPI::utils::EnumThirdPartyPlatform& platform, const Json::Value& config) {
    // 检查配置
    bool isNecessaryConfigsNotSet = false;
    std::string platformName = ThirdPartyPlatformToString(platform);

    // 检查ServerHost是否已经配置
    std::string serverHost;
    if (config["ServerHost"].isString()) {
        serverHost = config["ServerHost"].asString();
    }
    if (serverHost.empty()) {
        isNecessaryConfigsNotSet = true;
    }

    // 检查ClientId和Secret等是否已经配置
    std::string clientId, clientSecret;
    if (config["ThirdPartyPlatformInfo"][platformName]["ClientId"].isString()) {
        clientId = config["ThirdPartyPlatformInfo"][platformName]["ClientId"].asString();
    }
    if (config["ThirdPartyPlatformInfo"][platformName]["ClientSecret"].isString()) {
        clientSecret = config["ThirdPartyPlatformInfo"][platformName]["ClientSecret"].asString();
    }

    if (clientId.empty()) {
        LOG_ERROR << "关键信息未配置错误: '" << platformName << "'平台的ClientId未配置";
        isNecessaryConfigsNotSet = true;
    }
    if (clientSecret.empty()) {
        LOG_ERROR << "关键信息未配置错误: '" << platformName << "'平台的ClientSecret未配置";
        isNecessaryConfigsNotSet = true;
    }

    // 抛出异常
    if (isNecessaryConfigsNotSet) {
        throw std::runtime_error("关键配置未设置, 请查看日志以获取更多信息.");
    }

    return std::make_tuple(serverHost, clientId, clientSecret);
}

drogon::Task<std::shared_ptr<ThirdPartyLoginValue>> ThirdPartyLoginPlatformBase::getLoginValue(const std::string& code) {
    std::shared_lock<std::shared_mutex> lock(mutex);
    auto it = std::find_if(loginValues.begin(), loginValues.end(),
                          [&code](const std::shared_ptr<ThirdPartyLoginValue>& value) {
                              return value->code == code;
                          });
    co_return it != loginValues.end() ? *it : nullptr;
}

drogon::Task<bool> ThirdPartyLoginPlatformBase::verifyTheCode(const std::string& code, const std::string& verifyCode) {
    std::unique_lock<std::shared_mutex> lock(mutex);
    auto it = std::find_if(loginValues.begin(), loginValues.end(),
                          [&code](const std::shared_ptr<ThirdPartyLoginValue>& value) {
                              return value->code == code;
                          });

    if (it == loginValues.end()) {
        co_return false;
    }

    auto& value = *it;
    if (value->isExpired()) {
        loginValues.erase(it);
        co_return false;
    }

    co_return value->verifyCode == verifyCode && !value->authorizationCode.empty() && !value->consumed;
}

drogon::Task<std::shared_ptr<ThirdPartyLoginValue>> ThirdPartyLoginPlatformBase::createNewThirdLoginValue() {
    co_await clearExpired();
    std::unique_lock<std::shared_mutex> lock(mutex);

    std::string newCode;
    while (true) {
        newCode = RandomGenerator::getRandNumberStr(10);
        auto it = std::find_if(loginValues.begin(), loginValues.end(),
                              [&newCode](const std::shared_ptr<ThirdPartyLoginValue>& value) {
                                  return value->code == newCode;
                              });
        if (it == loginValues.end()) {
            break;
        }
    }

    auto newLoginValue = std::make_shared<ThirdPartyLoginValue>(newCode, RandomGenerator::getRandNumberStr(6));
    loginValues.push_back(newLoginValue);
    co_return newLoginValue;
}

drogon::Task<void> ThirdPartyLoginPlatformBase::clearExpired() {
    std::unique_lock<std::shared_mutex> lock(mutex);
    loginValues.erase(
        std::remove_if(loginValues.begin(), loginValues.end(),
                      [](const std::shared_ptr<ThirdPartyLoginValue>& value) {
                          return value->isExpired();
                      }),
        loginValues.end());
    co_return;
}

drogon::Task<void> ThirdPartyLoginPlatformBase::consumeLoginValue(const std::string& code) {
    std::unique_lock<std::shared_mutex> lock(mutex);
    auto it = std::find_if(loginValues.begin(), loginValues.end(),
                          [&code](const std::shared_ptr<ThirdPartyLoginValue>& value) {
                              return value->code == code;
                          });
    if (it != loginValues.end()) {
        (*it)->consumed = true;
    }
    co_return;
}

// ThirdPartyLoginPlatform_QQ 实现
ThirdPartyLoginPlatform_QQ::ThirdPartyLoginPlatform_QQ(const Json::Value& config)
    : ThirdPartyLoginPlatformBase(config, EnumThirdPartyPlatform::QQ) {
    // QQ使用特定的客户端
    httpClient = drogon::HttpClient::newHttpClient("https://graph.qq.com");
}

drogon::Task<bool> ThirdPartyLoginPlatform_QQ::fetchTokens(std::shared_ptr<ThirdPartyLoginValue> value) {
    if (value->code.empty()) {
        throw std::runtime_error("登录码为空, 请检查登录码是否正确.");
    }

    // 构造请求链接
    std::stringstream ss;
    ss << "/oauth2.0/token?grant_type=authorization_code"
       << "&client_id=" << clientId
       << "&client_secret=" << clientSecret
       << "&code=" << value->authorizationCode
       << "&redirect_uri=" << redirectUrl
       << "&fmt=json";

    // 发送请求
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath(ss.str());

    auto response = co_await httpClient->sendRequestCoro(req);
    
    
    if (!response) {
        LOG_ERROR << "QQ平台获取AccessToken失败, 无法获取响应";
        co_return false;
    }

    // 处理响应
    if (response->getStatusCode() != drogon::k200OK) {
        LOG_ERROR << "QQ平台获取AccessToken失败, 状态码: " << response->getStatusCode()
                  << ", 原因: " << response->getJsonError();
        co_return false;
    }

    // 解析JSON
    Json::Value json;
    Json::Reader reader;
    if (!reader.parse(std::string(response->getBody()), json)) {
        LOG_ERROR << "QQ平台获取AccessToken失败, 解析响应为JSON时失败, 响应原文: " << response->getBody();
        co_return false;
    }

    // 处理业务错误
    if (json.isMember("ret") && json["ret"].asInt() != 0) {
        LOG_ERROR << "QQ平台获取AccessToken失败, 错误码: " << json["ret"].asInt()
                  << ", 错误信息: " << json["msg"].asString();
        co_return false;
    }

    // 保存数据到Value
    if (json.isMember("access_token")) {
        value->accessToken = json["access_token"].asString();
    }
    if (json.isMember("refresh_token")) {
        value->refreshToken = json["refresh_token"].asString();
    }

    co_return true;
}

drogon::Task<std::string> ThirdPartyLoginPlatform_QQ::getAccessToken(std::shared_ptr<ThirdPartyLoginValue> value) {
    if (value->accessToken.empty()) {
        if (!co_await fetchTokens(value)) {
            LOG_ERROR << "获取QQ平台AccessToken失败.";
            co_return "";
        }
    }
    if (value->accessToken.empty()) {
        LOG_ERROR << "获取QQ平台AccessToken失败, FetchTokens后AccessToken仍为空.";
        co_return "";
    }

    co_return value->accessToken;
}

drogon::Task<bool> ThirdPartyLoginPlatform_QQ::fetchOpenId(std::shared_ptr<ThirdPartyLoginValue> value) {
    std::string accessToken = value->accessToken;
    if (accessToken.empty()) {
        LOG_ERROR << "获取QQ平台OpenId失败, AccessToken为空.";
        co_return false;
    }

    // 构造请求链接
    std::stringstream ss;
    ss << "/oauth2.0/me?access_token=" << accessToken << "&fmt=json";

    // 发送请求
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath(ss.str());

    auto response = co_await httpClient->sendRequestCoro(req);
    if (!response) {
        LOG_ERROR << "QQ平台获取OpenId失败, 无法获取响应";
        co_return false;
    }

    // 处理响应
    if (response->getStatusCode() != drogon::k200OK) {
        LOG_ERROR << "QQ平台获取OpenId失败, 状态码: " << response->getStatusCode()
                  << ", 原因: " << response->getJsonError();
        co_return false;
    }

    // 解析JSON
    Json::Value json;
    Json::Reader reader;
    if (!reader.parse(std::string(response->getBody()), json)) {
        LOG_ERROR << "QQ平台获取OpenId失败, 解析响应为JSON时失败, 响应原文: " << response->getBody();
        co_return false;
    }

    // 处理业务错误
    if (json.isMember("ret") && json["ret"].asInt() != 0) {
        LOG_ERROR << "QQ平台获取OpenId失败, 错误码: " << json["ret"].asInt()
                  << ", 错误信息: " << json["msg"].asString();
        co_return false;
    }

    // 保存数据到Value
    if (json.isMember("openid")) {
        value->openId = json["openid"].asString();
    }

    co_return true;
}

drogon::Task<std::string> ThirdPartyLoginPlatform_QQ::getOpenId(std::shared_ptr<ThirdPartyLoginValue> value) {
    if (!value->openId.empty()) {
        co_return value->openId;
    }

    std::string accessToken = co_await getAccessToken(value);
    if (accessToken.empty()) {
        LOG_ERROR << "获取QQ平台OpenId失败, AccessToken为空.";
        co_return "";
    }

    if (!co_await fetchOpenId(value)) {
        LOG_ERROR << "获取QQ平台OpenId失败.";
        co_return "";
    }

    co_return value->openId;
}

drogon::Task<std::string> ThirdPartyLoginPlatform_QQ::getAuthorizationUrl(std::shared_ptr<ThirdPartyLoginValue> value) {
    std::stringstream ss;
    ss << "https://graph.qq.com/oauth2.0/authorize?response_type=code"
       << "&client_id=" << clientId
       << "&redirect_uri=" << redirectUrl
       << "&state=" << value->code;

    co_return ss.str();
}

drogon::Task<bool> ThirdPartyLoginPlatform_QQ::fetchThirdPartyUserInfo(std::shared_ptr<ThirdPartyLoginValue> value) {
    std::string accessToken = value->accessToken;
    std::string openId = value->openId;

    if (accessToken.empty() || openId.empty()) {
        LOG_ERROR << "获取QQ平台用户信息失败, AccessToken或OpenId为空.";
        co_return false;
    }

    // 构造请求链接
    std::stringstream ss;
    ss << "/user/get_user_info"
       << "?access_token=" << accessToken
       << "&oauth_consumer_key=" << clientId
       << "&openid=" << openId
       << "&format=json";

    // 发送请求
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath(ss.str());

    auto response = co_await httpClient->sendRequestCoro(req);
    if (!response) {
        LOG_ERROR << "QQ平台获取用户信息失败, 无法获取响应";
        co_return false;
    }

    // 处理响应
    if (response->getStatusCode() != drogon::k200OK) {
        LOG_ERROR << "QQ平台获取用户信息失败, 状态码: " << response->getStatusCode()
                  << ", 原因: " << response->getJsonError();
        co_return false;
    }

    // 解析JSON
    Json::Value json;
    Json::Reader reader;
    if (!reader.parse(std::string(response->getBody()), json)) {
        LOG_ERROR << "QQ平台获取用户信息失败, 解析响应为JSON时失败, 响应原文: " << response->getBody();
        co_return false;
    }

    // 处理业务错误
    if (json.isMember("ret") && json["ret"].asInt() != 0) {
        LOG_ERROR << "QQ平台获取用户信息失败, 错误码: " << json["ret"].asInt()
                  << ", 错误信息: " << json["msg"].asString();
        co_return false;
    }

    // 保存数据到Value
    if (json.isMember("nickname")) {
        value->nickName = json["nickname"].asString();
    }
    if (json.isMember("figureurl_qq_2")) {
        value->avatarImgUrl = json["figureurl_qq_2"].asString();
    }

    co_return true;
}

drogon::Task<std::string> ThirdPartyLoginPlatform_QQ::getThirdPartyUserNickName(std::shared_ptr<ThirdPartyLoginValue> value) {
    if (!value->nickName.empty()) {
        co_return value->nickName;
    }

    if (!co_await fetchThirdPartyUserInfo(value)) {
        LOG_ERROR << "获取QQ平台用户昵称失败.";
        co_return "";
    }

    if (value->nickName.empty()) {
        LOG_ERROR << "获取QQ平台用户昵称失败, 获取到的昵称为空.";
        co_return "";
    }

    co_return value->nickName;
}

drogon::Task<bool> ThirdPartyLoginPlatform_QQ::callBack(const std::string& code, const std::string& state) {
    std::shared_ptr<ThirdPartyLoginValue> targetValue;
    {
        std::shared_lock<std::shared_mutex> lock(mutex);
        auto it = std::find_if(loginValues.begin(), loginValues.end(),
                              [&state](const std::shared_ptr<ThirdPartyLoginValue>& value) {
                                  return value->code == state;
                              });
        if (it == loginValues.end()) {
            LOG_ERROR << "QQ平台回调失败, 找不到对应的登录值, Code: " << code << ", State: " << state;
            co_return false;
        }
        targetValue = *it;
    }

    targetValue->authorizationCode = code;
    // TODO: 代码风格差异, 之后考虑要不要统一
    // TODO: 这样的get, set函数风格我觉得不好, getset应该属于loginvalue, 而不是platform, 也就是调用方式应为loginvalue->getAccessToken()这样,
    // 获取后会将数据存到loginValue中
    std::string accessToken = co_await getAccessToken(targetValue);
    std::string openId = co_await getOpenId(targetValue);
    std::string nickName = co_await getThirdPartyUserNickName(targetValue);
    // loginvalue续期(本来为2分钟有效期), 后续确认登录用
    targetValue->expireTime = std::chrono::system_clock::now() + std::chrono::minutes(5);
    
    LOG_INFO << "QQ平台回调成功, Code: " << code << ", State: " << state
             << " AccessToken = " << accessToken
             << " OpenId = " << openId
             << " NickName = " << nickName;

    co_return true;
}

drogon::Task<std::string> ThirdPartyLoginPlatform_QQ::saveInfoToDB(std::shared_ptr<ThirdPartyLoginValue> value)
{
    throw std::runtime_error("Not Implemented.");
}

// ThirdPartyLoginPlatform_WeChat 实现
ThirdPartyLoginPlatform_WeChat::ThirdPartyLoginPlatform_WeChat(const Json::Value& config)
    : ThirdPartyLoginPlatformBase(config, EnumThirdPartyPlatform::WeChat) {
    // 微信使用特定的客户端
    httpClient = drogon::HttpClient::newHttpClient("https://api.weixin.qq.com");
}

drogon::Task<bool> ThirdPartyLoginPlatform_WeChat::fetchTokens(std::shared_ptr<ThirdPartyLoginValue> value) {
    if (value->code.empty()) {
        throw std::runtime_error("登录码为空, 请检查登录码是否正确.");
    }

    // 构造请求链接
    std::stringstream ss;
    ss << "/sns/oauth2/access_token"
       << "?appid=" << clientId
       << "&secret=" << clientSecret
       << "&code=" << value->authorizationCode
       << "&grant_type=authorization_code";

    // 发送请求
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath(ss.str());

    auto response = co_await httpClient->sendRequestCoro(req);
    if (!response) {
        LOG_ERROR << "微信平台获取AccessToken失败, 无法获取响应";
        co_return false;
    }

    // 处理响应
    if (response->getStatusCode() != drogon::k200OK) {
        LOG_ERROR << "微信平台获取AccessToken失败, 状态码: " << response->getStatusCode()
                  << ", 原因: " << response->getJsonError();
        co_return false;
    }

    // 解析JSON
    Json::Value json;
    Json::Reader reader;
    if (!reader.parse(std::string(response->getBody()), json)) {
        LOG_ERROR << "微信平台获取AccessToken失败, 解析响应为JSON时失败, 响应原文: " << response->getBody();
        co_return false;
    }

    // 微信比较特殊, 错误时返回另一结构的json数据, 不能直接解析
    if (json.isMember("errcode")) {
        LOG_ERROR << "微信平台获取AccessToken失败, 错误码: " << json["errcode"].asInt()
                  << ", 错误信息: " << json["errmsg"].asString();
        co_return false;
    }

    // 保存数据到Value
    if (json.isMember("openid")) {
        value->openId = json["openid"].asString();
    }
    if (json.isMember("access_token")) {
        value->accessToken = json["access_token"].asString();
    }
    if (json.isMember("refresh_token")) {
        value->refreshToken = json["refresh_token"].asString();
    }

    co_return true;
}

drogon::Task<std::string> ThirdPartyLoginPlatform_WeChat::getAccessToken(std::shared_ptr<ThirdPartyLoginValue> value) {
    if (value->accessToken.empty()) {
        if (!co_await fetchTokens(value)) {
            LOG_ERROR << "获取微信平台AccessToken失败.";
            co_return "";
        }
    }
    if (value->accessToken.empty()) {
        LOG_ERROR << "获取微信平台AccessToken失败, FetchTokens后AccessToken仍为空.";
        co_return "";
    }

    co_return value->accessToken;
}

drogon::Task<bool> ThirdPartyLoginPlatform_WeChat::fetchOpenId(std::shared_ptr<ThirdPartyLoginValue> value) {
    // 微信平台在获取Token的同时也获取了OpenId, 所以不需要单独获取OpenId. 但接口定义了这个方法, 必须要实现
    co_return co_await fetchTokens(value);
}

drogon::Task<std::string> ThirdPartyLoginPlatform_WeChat::getOpenId(std::shared_ptr<ThirdPartyLoginValue> value) {
    if (!value->openId.empty()) {
        co_return value->openId;
    }

    std::string accessToken = co_await getAccessToken(value);
    if (accessToken.empty()) {
        LOG_ERROR << "获取微信平台OpenId失败, AccessToken为空.";
        co_return "";
    }

    if (!co_await fetchOpenId(value)) {
        LOG_ERROR << "获取微信平台OpenId失败.";
        co_return "";
    }

    co_return value->openId;
}

drogon::Task<std::string> ThirdPartyLoginPlatform_WeChat::getAuthorizationUrl(std::shared_ptr<ThirdPartyLoginValue> value) {
    std::stringstream ss;
    ss << "https://open.weixin.qq.com/connect/qrconnect"
       << "?appid=" << clientId
       << "&redirect_uri=" << redirectUrl
       << "&state=" << value->code
       << "&scope=snsapi_login"
       << "&response_type=code";

    co_return ss.str();
}

drogon::Task<bool> ThirdPartyLoginPlatform_WeChat::fetchThirdPartyUserInfo(std::shared_ptr<ThirdPartyLoginValue> value) {
    std::string accessToken = value->accessToken;
    std::string openId = value->openId;

    if (accessToken.empty() || openId.empty()) {
        LOG_ERROR << "获取微信平台用户信息失败, AccessToken或OpenId为空.";
        co_return false;
    }

    // 构造请求链接
    std::stringstream ss;
    ss << "/sns/userinfo"
       << "?access_token=" << accessToken
       << "&openid=" << openId;

    // 发送请求
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath(ss.str());

    auto response = co_await httpClient->sendRequestCoro(req);
    if (!response) {
        LOG_ERROR << "微信平台获取用户信息失败, 无法获取响应";
        co_return false;
    }

    // 处理响应
    if (response->getStatusCode() != drogon::k200OK) {
        LOG_ERROR << "微信平台获取用户信息失败, 状态码: " << response->getStatusCode()
                  << ", 原因: " << response->getJsonError();
        co_return false;
    }

    // 解析JSON
    Json::Value json;
    Json::Reader reader;
    if (!reader.parse(std::string(response->getBody()), json)) {
        LOG_ERROR << "微信平台获取用户信息失败, 解析响应为JSON时失败, 响应原文: " << response->getBody();
        co_return false;
    }

    // 处理业务错误
    if (json.isMember("errcode")) {
        LOG_ERROR << "微信平台获取用户信息失败, 错误码: " << json["errcode"].asInt()
                  << ", 错误信息: " << json["errmsg"].asString();
        co_return false;
    }

    // 保存数据到Value
    if (json.isMember("nickname")) {
        value->nickName = json["nickname"].asString();
    }
    if (json.isMember("headimgurl")) {
        value->avatarImgUrl = json["headimgurl"].asString();
    }

    co_return true;
}

drogon::Task<std::string> ThirdPartyLoginPlatform_WeChat::getThirdPartyUserNickName(std::shared_ptr<ThirdPartyLoginValue> value) {
    if (!value->nickName.empty()) {
        co_return value->nickName;
    }

    if (!co_await fetchThirdPartyUserInfo(value)) {
        LOG_ERROR << "获取微信平台用户昵称失败.";
        co_return "";
    }

    if (value->nickName.empty()) {
        LOG_ERROR << "获取微信平台用户昵称失败, 获取到的昵称为空.";
        co_return "";
    }

    co_return value->nickName;
}

drogon::Task<bool> ThirdPartyLoginPlatform_WeChat::callBack(const std::string& code, const std::string& state) {
    std::shared_ptr<ThirdPartyLoginValue> targetValue;
    {
        std::shared_lock<std::shared_mutex> lock(mutex);
        auto it = std::find_if(loginValues.begin(), loginValues.end(),
                              [&state](const std::shared_ptr<ThirdPartyLoginValue>& value) {
                                  return value->code == state;
                              });
        if (it == loginValues.end()) {
            LOG_ERROR << "微信平台回调失败, 找不到对应的登录值, Code: " << code << ", State: " << state;
            co_return false;
        }
        targetValue = *it;
    }

    targetValue->authorizationCode = code;
    std::string accessToken = co_await getAccessToken(targetValue);
    std::string openId = co_await getOpenId(targetValue);
    std::string nickName = co_await getThirdPartyUserNickName(targetValue);
    
    LOG_INFO << "微信平台回调成功, Code: " << code << ", State: " << state
             << " AccessToken = " << accessToken
             << " OpenId = " << openId
             << " NickName = " << nickName;

    co_return true;
}

drogon::Task<std::string> ThirdPartyLoginPlatform_WeChat::saveInfoToDB(std::shared_ptr<ThirdPartyLoginValue> value)
{
    throw std::runtime_error("Not implemented.");
}

// ThirdPartyLoginService 实现
ThirdPartyLoginService::ThirdPartyLoginService(const Json::Value& config) {
    // 添加QQ平台
    platforms[EnumThirdPartyPlatform::QQ] = std::make_unique<ThirdPartyLoginPlatform_QQ>(config);

    // 添加微信平台
    platforms[EnumThirdPartyPlatform::WeChat] = std::make_unique<ThirdPartyLoginPlatform_WeChat>(config);
}

drogon::Task<IThirdPartyLoginPlatform*> ThirdPartyLoginService::getPlatform(UEAdminAPI::utils::EnumThirdPartyPlatform platform) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = platforms.find(platform);
    co_return it != platforms.end() ? it->second.get() : nullptr;
}

drogon::Task<IThirdPartyLoginPlatform*> ThirdPartyLoginService::getPlatform(const std::string& platform){
    // 转换为小写
    co_return co_await getPlatform(getPlatformFromString(platform));
}

drogon::Task<std::tuple<UEAdminAPI::utils::EnumThirdPartyPlatform, std::shared_ptr<ThirdPartyLoginValue>>> ThirdPartyLoginService::getCodeAndItsPlatform(const std::string &code) {
    std::lock_guard<std::mutex> lock(mutex);
    for(const auto& platform: platforms) {
        auto value = co_await platform.second->getLoginValue(code);
        if(!value){
            continue;
        }
        co_return std::make_tuple(platform.first, value);
    }
}

drogon::Task<void> ThirdPartyLoginService::deletePlatform(UEAdminAPI::utils::EnumThirdPartyPlatform platform) {
    std::lock_guard<std::mutex> lock(mutex);
    platforms.erase(platform);
    co_return;
}

drogon::Task<void> ThirdPartyLoginService::clearExpired() {
    for (auto& [platform, platformImpl] : platforms) {
        co_await platformImpl->clearExpired();
    }
    co_return;
}



drogon::Task<UEAdminAPI::utils::HttpResult> ThirdPartyLoginService::GetLoginUrl(const std::string &platform) {
    auto platformService = co_await getPlatform(platform);

    UEAdminAPI::utils::HttpResult result;

    if (!platformService) {
        result.setResult(-1, "不支持的第三方平台");
        co_return result;
    }
    auto loginValue = co_await platformService->createNewThirdLoginValue();
    std::string authUrl = co_await platformService->getAuthorizationUrl(loginValue);

    result.jsondata["code"] = loginValue->code;
    result.jsondata["verifyCode"] = loginValue->verifyCode;
    result.jsondata["authorizationUrl"] = authUrl;
    co_return result;
}

drogon::Task<UEAdminAPI::utils::HttpResult> ThirdPartyLoginService::Callback(const std::string &platform, const std::string &code, const std::string &state) {
    auto platformService = co_await getPlatform(platform);

    UEAdminAPI::utils::HttpResult result;

    if (!platformService) {
        result.setResult(-1, "不支持的第三方平台");
        co_return result;
    }
    if (!co_await platformService->callBack(code, state)) {
        result.setResult(-1, "处理第三方登录回调失败, 可能是登录操作超时");
        co_return result;
    }
    if (!co_await platformService->getLoginValue(state)) {
        result.setResult(-1, "找不到对应的登录值");
        co_return result;
    }
    co_return result;
}

Task<HttpResult> ThirdPartyLoginService::BindAccount(const std::string &token, const std::string &platform, const std::string &code, const std::string &verifyCode) {
    auto _authService = AuthService::Instance();

    HttpResult result;
    
    if (platform.empty() || code.empty() || verifyCode.empty() || token.empty()) {
        result.setResult(-1, "缺少必要参数");
        co_return result;
    }
    
    result = co_await _authService->VerifyToken(token);
    if(result.jsondata["tokenType"].asString() != "token"){
        result.setResult(-1, "不是token");
        co_return result;
    }
    if(result.code != 0){
        result.setResult(-1, "Token已失效");
        co_return result;
    }
    int userId = result.jsondata["userId"].asInt();

    auto dbClientPtr = drogon::app().getDbClient();
    Mapper<User> mapperUser(dbClientPtr);
    Mapper<UserThirdPartyInfo> mapperThirdPartyInfo(dbClientPtr);
    Mapper<ThirdPartyPlatforms> mapperThirdPartyPlatforms(dbClientPtr);

    // 验证登录
    result = co_await VerifyLogin(platform, code, verifyCode);
    if(result.code != 0){
        if(result.jsondata["allready_bind"] == true){
            result.setResult(-1, "该平台已经绑定过账号");
            co_return result;
        }
        result.setResult(-1, "第三方登陆验证失败");
        co_return result;
    }

    User targetUser;
    try {
        targetUser = mapperUser.findOne(Criteria(User::Cols::_id, CompareOperator::EQ, userId));
    } catch (const drogon::orm::DrogonDbException &ex) {
        result.setResult(-1, "内部错误");
        co_return result;
    }
    auto thirdPartyPlatform = co_await getPlatform(platform);
    if (!thirdPartyPlatform) {
        result.setResult(-1, "不支持的第三方平台");
        co_return result;
    }
    if (!(co_await thirdPartyPlatform->verifyTheCode(code, verifyCode))) {
        result.setResult(-1, "验证码错误");
        co_return result;
    }
    auto thirdPartyInfos = targetUser.getThird_party_platforms(dbClientPtr);
    auto targetInfos = std::find_if(thirdPartyInfos.begin(), thirdPartyInfos.end(), [thirdPartyPlatform](const std::pair<ThirdPartyPlatforms, UserThirdPartyInfo> &info){
        return info.first.getValueOfPlatformName() == ThirdPartyPlatformToString(thirdPartyPlatform->getPlatform());
    });
    if (targetInfos != thirdPartyInfos.end()) {
        result.setResult(-1, "该平台已经绑定过账号");
        co_return result;
    }

    auto loginValue = co_await thirdPartyPlatform->getLoginValue(code);
    UserThirdPartyInfo thirdPartyInfo;
    thirdPartyInfo.setUserId(targetUser.getValueOfId());
    thirdPartyInfo.setPlatformId(int(thirdPartyPlatform->getPlatform()));
    thirdPartyInfo.setAccessToken(loginValue->accessToken);
    thirdPartyInfo.setNickName(loginValue->nickName);
    thirdPartyInfo.setOpenId(loginValue->openId);
    thirdPartyInfo.setAvatarImgUrl(loginValue->avatarImgUrl);
    try {
        mapperThirdPartyInfo.insert(thirdPartyInfo);
    } catch (const drogon::orm::DrogonDbException &ex) {
        result.setResult(-1, "绑定第三方账号失败");
        co_return result;
    }
    result.setResult(0, "绑定成功");
    co_await thirdPartyPlatform->consumeLoginValue(code);
    co_return result;
}

drogon::Task<UEAdminAPI::utils::HttpResult> ThirdPartyLoginService::VerifyLogin(const std::string &platform, const std::string &code, const std::string &verifyCode, bool onlyCheck) {
    auto _authService = AuthService::Instance();
    
    UEAdminAPI::utils::HttpResult result;

    auto platformService = co_await getPlatform(platform);
    if (!platformService) {
        result.setResult(-1, "不支持的第三方平台");
        co_return result;
    }
    if (code.empty() || verifyCode.empty()) {
        result.setResult(-1, "缺少必要参数, code 和 verifyCode");
        co_return result;
    }
    bool success = co_await platformService->verifyTheCode(code, verifyCode);
    if (!success) {
        result.setResult(-1, "验证失败");
        co_return result;
    }
    auto loginValue = co_await platformService->getLoginValue(code);
    if (!loginValue) {
        result.setResult(-1, "找不到对应的登录值");
        co_return result;
    }
    if (loginValue->authorizationCode.empty()) {
        result.setResult(-1, "该code尚未登录, 验证失败");
        co_return result;
    }
    auto dbClientPtr = drogon::app().getDbClient();
    Mapper<UserThirdPartyInfo> mapperThirdPartyInfo(dbClientPtr);
    Mapper<ThirdPartyPlatforms> mapperThirdPartyPlatforms(dbClientPtr);
    bool isAllreadyBind = true;
    UserThirdPartyInfo thirdPartyInfo;
    try {
        thirdPartyInfo = mapperThirdPartyInfo.findOne(
            Criteria(UserThirdPartyInfo::Cols::_open_id, CompareOperator::EQ, loginValue->openId) &&
            Criteria(UserThirdPartyInfo::Cols::_platform_id, CompareOperator::EQ, int(platformService->getPlatform())));
    } catch (const drogon::orm::UnexpectedRows &e) {
        // 没找到或是找到多个, 后者不太可能, 所以不特殊处理了
        // 没找到表示该平台未绑定过该账号
        isAllreadyBind = false;
    }

    // 这里返回成功并不是说这个第三方账号可以绑定, 而仅是表示code对应的那一次扫码登录完成了
    // 后续需要根据isAllreadyBind来判断是否需要创建用户
    result.setResult(0, "验证成功");
    result.jsondata["allready_bind"] = isAllreadyBind;

    // 如果只是确认登录, 则直接返回
    if (onlyCheck) {
        co_return result;
    }

    // 如果不是确认登录, 则需要根据isAllreadyBind来判断是否需要已经绑定
    // 如果绑定则登录, 否则返回提示信息
    if (!isAllreadyBind) {
        result.setResult(-1, "该平台未绑定过该账号");
        co_return result;
    }

    // 如果绑定了, 则登录
    result = co_await _authService->LoginByUserId(thirdPartyInfo.getValueOfUserId());
    co_await platformService->consumeLoginValue(code);

    co_return result;
}

drogon::Task<UEAdminAPI::utils::HttpResult> ThirdPartyLoginService::CreateUserFromThirdPartyAndLogin(const std::string &platform, const std::string &code, const std::string &verifyCode) {
    auto _authService = AuthService::Instance();

    UEAdminAPI::utils::HttpResult result;
    auto platformService = co_await getPlatform(platform);
    if (!platformService) {
        result.setResult(-1, "不支持的第三方平台");
        co_return result;
    }
    if (code.empty() || verifyCode.empty()) {
        result.setResult(-1, "缺少必要参数, code 和 verifyCode");
        co_return result;
    }
    bool success = co_await platformService->verifyTheCode(code, verifyCode);
    if (!success) {
        result.setResult(-1, "验证失败");
        co_return result;
    }
    auto loginValue = co_await platformService->getLoginValue(code);
    if (!loginValue) {
        result.setResult(-1, "找不到对应的登录值");
        co_return result;
    }
    if (loginValue->authorizationCode.empty()) {
        result.setResult(-1, "该code尚未登录, 验证失败");
        co_return result;
    }
    std::string username = "NewUser_" + RandomGenerator::getRandNumberStr(8);
    std::string password = RandomGenerator::generateRandomPassword();
    auto dbClientPtr = drogon::app().getDbClient();
    Mapper<User> mapperUser(dbClientPtr);
    Mapper<UserThirdPartyInfo> mapperThirdPartyInfo(dbClientPtr);
    Mapper<ThirdPartyPlatforms> mapperThirdPartyPlatforms(dbClientPtr);
    int renameTryCount = 0;
    while (true) {
        if (renameTryCount > 10) {
            result.setResult(-1, "创建用户失败");
            co_return result;
        }
        try {
            mapperUser.findOne(Criteria(User::Cols::_name, CompareOperator::EQ, username));
            username = "NewUser_" + RandomGenerator::getRandNumberStr(8);
            renameTryCount++;
        } catch (const drogon::orm::UnexpectedRows &e) {
            if (std::string(e.what()) == "0 rows found") {
                break;
            } else if (std::string(e.what()) == "Found more than one row") {
                result.setResult(-1, "创建用户失败");
                co_return result;
            }
        }
    }
    auto [hash, salt] = _authService->CreateStrPasswordHash(password);
    User user;
    user.setName(username);
    user.setNickName(username);
    user.setPasswordHash(hash);
    user.setPasswordSalt(salt);
    user.setCreateAt(trantor::Date::now());
    user.setIsMale(true);
    user.setPrivilege(int(UserPrivileges::User));
    bool isUserInsertFailed = false;
    try {
        mapperUser.insert(user);
    } catch (const drogon::orm::DrogonDbException &e) {
        isUserInsertFailed = true;
    }
    if (isUserInsertFailed) {
        result.setResult(-1, "创建用户失败");
        co_return result;
    }
    UserThirdPartyInfo thirdPartyInfo;
    thirdPartyInfo.setUserId(user.getValueOfId()); 
    thirdPartyInfo.setPlatformId(int(platformService->getPlatform()));
    thirdPartyInfo.setOpenId(loginValue->openId);
    thirdPartyInfo.setAccessToken(loginValue->accessToken);
    thirdPartyInfo.setNickName(loginValue->nickName);
    thirdPartyInfo.setAvatarImgUrl(loginValue->avatarImgUrl);
    isUserInsertFailed = false;
    try {
        mapperThirdPartyInfo.insert(thirdPartyInfo);
    } catch (const drogon::orm::DrogonDbException &e) {
        isUserInsertFailed = true;
    }
    if (isUserInsertFailed) {
        mapperUser.deleteOne(user);
        result.setResult(-1, "创建第三方登录信息失败");
        co_return result;
    }

    // 登录
    result = co_await _authService->LoginByUserId(user.getValueOfId());
    co_await platformService->consumeLoginValue(code);
    
    co_return result;
}

drogon::Task<UEAdminAPI::utils::HttpResult> ThirdPartyLoginService::LoginWithThirdParty(const std::string &platform, const std::string &code, const std::string &verifyCode) {
    UEAdminAPI::utils::HttpResult result;
    
    // 1. 验证第三方登录
    result = co_await VerifyLogin(platform, code, verifyCode);
    if (result.code != 0) {
        co_return result;
    }

    // 2. 检查是否已绑定
    if (!result.jsondata["allready_bind"].asBool()) {
        result.setResult(-1, "该第三方账号未绑定任何用户");
        co_return result;
    }

    // 3. 获取LoginValue
    auto platformService = co_await getPlatform(platform);
    // 这里不需要再检查platformService是否存在, VerifyLogin已经检查过了
    auto loginValue = co_await platformService->getLoginValue(code);
    
    // 4. 查找绑定的UserThirdPartyInfo
    auto dbClientPtr = drogon::app().getDbClient();
    Mapper<UserThirdPartyInfo> mapperThirdPartyInfo(dbClientPtr);
    UserThirdPartyInfo info;
    try {
        info = mapperThirdPartyInfo.findOne(
            Criteria(UserThirdPartyInfo::Cols::_open_id, CompareOperator::EQ, loginValue->openId) &&
            Criteria(UserThirdPartyInfo::Cols::_platform_id, CompareOperator::EQ, int(platformService->getPlatform())));
    } catch (const drogon::orm::UnexpectedRows &e) {
        // 理论上 VerifyLogin 返回 allready_bind=true 时这里一定能找到
        result.setResult(-1, "内部错误");
        LOG_ERROR << "内部数据不一致, 提示已绑定但找不到对应的数据库记录";
        co_return result;
    }

    // 5. 调用AuthService完成登录
    auto authService = AuthService::Instance();
    result = co_await authService->LoginByUserId(info.getValueOfUserId());
    co_await platformService->consumeLoginValue(code);
    
    co_return result;
}

drogon::Task<UEAdminAPI::utils::HttpResult> ThirdPartyLoginService::UnbindAccount(const std::string &token, const std::string &platform, const std::string &target, const std::string &verifyCode) {
    auto _authService = AuthService::Instance();
    auto _mfaService = MFAService::Instance();

    UEAdminAPI::utils::HttpResult result;

    if (platform.empty()) {
        result.setResult(-1, "缺少参数 platform");
        co_return result;
    }
    if (target.empty()) {
        result.setResult(-1, "缺少参数 target");
        co_return result;
    }
    if (token.empty()) {
        result.setResult(-1, "缺少参数 token");
        co_return result;
    }
    if (verifyCode.empty()) {
        result.setResult(-1, "缺少参数 verifyCode");
        co_return result;
    }

    result = co_await _authService->VerifyToken(token);
    if(result.jsondata["tokenType"].asString() != "token"){
        result.setResult(-1, "不是token");
        co_return result;
    }
    if(result.code != 0){
        result.setResult(-1, "Token已失效");
        co_return result;
    }
    int userId = result.jsondata["userId"].asInt();

    auto platformService = co_await getPlatform(platform);
    if (!platformService) {
        result.setResult(-1, "不支持的第三方平台");
        co_return result;
    }

    auto dbClientPtr = drogon::app().getDbClient();
    Mapper<User> mapperUser(dbClientPtr);
    Mapper<UserThirdPartyInfo> mapperThirdPartyInfo(dbClientPtr);
    // 获取用户信息以推断默认目标
    User user;
    try {
        user = mapperUser.findByPrimaryKey(userId);
    } catch (const drogon::orm::UnexpectedRows &e) {
        result.setResult(-1, "数据库错误");
        co_return result;
    }

    std::string realTarget = target;
    if (realTarget.empty()) {
        if (user.getEmail()) {
            realTarget = user.getValueOfEmail();
        } else if (user.getTelephoneNumber()) {
            realTarget = user.getValueOfTelephoneNumber();
        } else {
            result.setResult(-1, "无法确定MFA目标，请提供target");
            co_return result;
        }
    }

    auto chType = MFAChannelBase::DetermineChannelType(realTarget);
    if (chType == eChannelType::Email) {
        if (!user.getEmail() || user.getValueOfEmail() != realTarget) {
            result.setResult(-1, "目标未绑定到当前用户");
            co_return result;
        }
    } else if (chType == eChannelType::SMS) {
        auto [cc, pn] = CodePairBase::SMSCodePair::ParsePhoneNumber(realTarget);
        if (!user.getTelephoneNumber() || user.getValueOfTelephoneNumber() != pn) {
            result.setResult(-1, "目标未绑定到当前用户");
            co_return result;
        }
    } else {
        result.setResult(-1, "无法判断渠道类型");
        co_return result;
    }

    auto [ok, msg] = co_await _mfaService->VerifyTheCode(realTarget, verifyCode, eMFAType::ThirdPartyBind);
    if (!ok) {
        result.setResult(-1, msg.empty() ? "验证码错误" : msg);
        co_return result;
    }

    bool notBound = false;
    try {
        // 检查是否存在绑定记录
        auto info = mapperThirdPartyInfo.findOne(
            Criteria(UserThirdPartyInfo::Cols::_user_id, CompareOperator::EQ, userId) &&
            Criteria(UserThirdPartyInfo::Cols::_platform_id, CompareOperator::EQ, int(platformService->getPlatform()))
        );
        // 删除绑定记录
        mapperThirdPartyInfo.deleteOne(info);
    } catch (const drogon::orm::UnexpectedRows &e) {
        if (std::string(e.what()) == "0 rows found") {
            notBound = true;
        } else {
            result.setResult(-1, "内部错误");
            co_return result;
        }
    }

    if (notBound) {
        result.setResult(-1, "该平台未绑定账号");
        co_return result;
    }

    result.setResult(0, "解绑成功");
    co_return result;
}

} // namespace Services
} // namespace UEAdminAPI

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
#include <numeric>

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
    std::lock_guard<std::recursive_mutex> valueLock(value->mutex);
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
                          std::lock_guard<std::recursive_mutex> valueLock(value->mutex);
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
        std::lock_guard<std::recursive_mutex> valueLock((*it)->mutex);
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
    std::string authCode;
    {
        std::lock_guard<std::recursive_mutex> lock(value->mutex);
        authCode = value->authorizationCode;
    }

    if (authCode.empty()) {
        throw std::runtime_error("登录码为空, 请检查登录码是否正确.");
    }

    // 构造请求链接
    std::stringstream ss;
    ss << "/oauth2.0/token?grant_type=authorization_code"
       << "&client_id=" << clientId
       << "&client_secret=" << clientSecret
       << "&code=" << authCode
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
    {
        std::lock_guard<std::recursive_mutex> lock(value->mutex);
        if (json.isMember("access_token")) {
            value->accessToken = json["access_token"].asString();
        }
        if (json.isMember("refresh_token")) {
            value->refreshToken = json["refresh_token"].asString();
        }
    }

    co_return true;
}

drogon::Task<std::string> ThirdPartyLoginPlatform_QQ::getAccessToken(std::shared_ptr<ThirdPartyLoginValue> value) {
    {
        std::lock_guard<std::recursive_mutex> lock(value->mutex);
        if (!value->accessToken.empty()) {
            co_return value->accessToken;
        }
    }

    if (!co_await fetchTokens(value)) {
        LOG_ERROR << "获取QQ平台AccessToken失败.";
        co_return "";
    }

    {
        std::lock_guard<std::recursive_mutex> lock(value->mutex);
        if (value->accessToken.empty()) {
            LOG_ERROR << "获取QQ平台AccessToken失败, FetchTokens后AccessToken仍为空.";
            co_return "";
        }
        co_return value->accessToken;
    }
}

drogon::Task<bool> ThirdPartyLoginPlatform_QQ::fetchOpenId(std::shared_ptr<ThirdPartyLoginValue> value) {
    std::string accessToken;
    {
        std::lock_guard<std::recursive_mutex> lock(value->mutex);
        accessToken = value->accessToken;
    }

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
        std::lock_guard<std::recursive_mutex> lock(value->mutex);
        value->openId = json["openid"].asString();
    }

    co_return true;
}

drogon::Task<std::string> ThirdPartyLoginPlatform_QQ::getOpenId(std::shared_ptr<ThirdPartyLoginValue> value) {
    {
        std::lock_guard<std::recursive_mutex> lock(value->mutex);
        if (!value->openId.empty()) {
            co_return value->openId;
        }
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

    {
        std::lock_guard<std::recursive_mutex> lock(value->mutex);
        co_return value->openId;
    }
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
    std::string accessToken;
    std::string openId;
    {
        std::lock_guard<std::recursive_mutex> lock(value->mutex);
        accessToken = value->accessToken;
        openId = value->openId;
    }

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
    {
        std::lock_guard<std::recursive_mutex> lock(value->mutex);
        if (json.isMember("nickname")) {
            value->nickName = json["nickname"].asString();
        }
        if (json.isMember("figureurl_qq_2")) {
            value->avatarImgUrl = json["figureurl_qq_2"].asString();
        }
    }

    co_return true;
}

drogon::Task<std::string> ThirdPartyLoginPlatform_QQ::getThirdPartyUserNickName(std::shared_ptr<ThirdPartyLoginValue> value) {
    {
        std::lock_guard<std::recursive_mutex> lock(value->mutex);
        if (!value->nickName.empty()) {
            co_return value->nickName;
        }
    }

    if (!co_await fetchThirdPartyUserInfo(value)) {
        LOG_ERROR << "获取QQ平台用户昵称失败.";
        co_return "";
    }

    {
        std::lock_guard<std::recursive_mutex> lock(value->mutex);
        if (value->nickName.empty()) {
            LOG_ERROR << "获取QQ平台用户昵称失败, 获取到的昵称为空.";
            co_return "";
        }
        co_return value->nickName;
    }
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

    {
        std::lock_guard<std::recursive_mutex> valueLock(targetValue->mutex);
        targetValue->authorizationCode = code;
    }

    // TODO: 代码风格差异, 之后考虑要不要统一
    // TODO: 这样的get, set函数风格我觉得不好, getset应该属于loginvalue, 而不是platform, 也就是调用方式应为loginvalue->getAccessToken()这样,
    // 获取后会将数据存到loginValue中
    std::string accessToken = co_await getAccessToken(targetValue);
    std::string openId = co_await getOpenId(targetValue);
    std::string nickName = co_await getThirdPartyUserNickName(targetValue);
    
    {
        std::lock_guard<std::recursive_mutex> valueLock(targetValue->mutex);
        // loginvalue续期(本来为2分钟有效期), 后续确认登录用
        targetValue->expireTime = std::chrono::system_clock::now() + std::chrono::minutes(5);
        targetValue->ready = true;
    }
    
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
    std::string authCode;
    {
        std::lock_guard<std::recursive_mutex> lock(value->mutex);
        authCode = value->authorizationCode;
    }

    if (authCode.empty()) {
        throw std::runtime_error("登录码为空, 请检查登录码是否正确.");
    }

    // 构造请求链接
    std::stringstream ss;
    ss << "/sns/oauth2/access_token"
       << "?appid=" << clientId
       << "&secret=" << clientSecret
       << "&code=" << authCode
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
    {
        std::lock_guard<std::recursive_mutex> lock(value->mutex);
        if (json.isMember("openid")) {
            value->openId = json["openid"].asString();
        }
        if (json.isMember("access_token")) {
            value->accessToken = json["access_token"].asString();
        }
        if (json.isMember("refresh_token")) {
            value->refreshToken = json["refresh_token"].asString();
        }
    }

    co_return true;
}

drogon::Task<std::string> ThirdPartyLoginPlatform_WeChat::getAccessToken(std::shared_ptr<ThirdPartyLoginValue> value) {
    {
        std::lock_guard<std::recursive_mutex> lock(value->mutex);
        if (!value->accessToken.empty()) {
            co_return value->accessToken;
        }
    }

    if (!co_await fetchTokens(value)) {
        LOG_ERROR << "获取微信平台AccessToken失败.";
        co_return "";
    }

    {
        std::lock_guard<std::recursive_mutex> lock(value->mutex);
        if (value->accessToken.empty()) {
            LOG_ERROR << "获取微信平台AccessToken失败, FetchTokens后AccessToken仍为空.";
            co_return "";
        }
        co_return value->accessToken;
    }
}

drogon::Task<bool> ThirdPartyLoginPlatform_WeChat::fetchOpenId(std::shared_ptr<ThirdPartyLoginValue> value) {
    // 微信平台在获取Token的同时也获取了OpenId, 所以不需要单独获取OpenId. 但接口定义了这个方法, 必须要实现
    co_return co_await fetchTokens(value);
}

drogon::Task<std::string> ThirdPartyLoginPlatform_WeChat::getOpenId(std::shared_ptr<ThirdPartyLoginValue> value) {
    {
        std::lock_guard<std::recursive_mutex> lock(value->mutex);
        if (!value->openId.empty()) {
            co_return value->openId;
        }
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

    {
        std::lock_guard<std::recursive_mutex> lock(value->mutex);
        co_return value->openId;
    }
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
    std::string accessToken;
    std::string openId;
    {
        std::lock_guard<std::recursive_mutex> lock(value->mutex);
        accessToken = value->accessToken;
        openId = value->openId;
    }

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
    {
        std::lock_guard<std::recursive_mutex> lock(value->mutex);
        if (json.isMember("nickname")) {
            value->nickName = json["nickname"].asString();
        }
        if (json.isMember("headimgurl")) {
            value->avatarImgUrl = json["headimgurl"].asString();
        }
    }

    co_return true;
}

drogon::Task<std::string> ThirdPartyLoginPlatform_WeChat::getThirdPartyUserNickName(std::shared_ptr<ThirdPartyLoginValue> value) {
    {
        std::lock_guard<std::recursive_mutex> lock(value->mutex);
        if (!value->nickName.empty()) {
            co_return value->nickName;
        }
    }

    if (!co_await fetchThirdPartyUserInfo(value)) {
        LOG_ERROR << "获取微信平台用户昵称失败.";
        co_return "";
    }

    {
        std::lock_guard<std::recursive_mutex> lock(value->mutex);
        if (value->nickName.empty()) {
            LOG_ERROR << "获取微信平台用户昵称失败, 获取到的昵称为空.";
            co_return "";
        }
        co_return value->nickName;
    }
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

    {
        std::lock_guard<std::recursive_mutex> valueLock(targetValue->mutex);
        targetValue->authorizationCode = code;
    }

    std::string accessToken = co_await getAccessToken(targetValue);
    std::string openId = co_await getOpenId(targetValue);
    std::string nickName = co_await getThirdPartyUserNickName(targetValue);
    
    {
        std::lock_guard<std::recursive_mutex> valueLock(targetValue->mutex);
        targetValue->ready = true;
    }
    
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
    std::vector<IThirdPartyLoginPlatform*> platformsCopy;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for(auto& pair : platforms) {
            platformsCopy.push_back(pair.second.get());
        }
    }

    for (auto* platformImpl : platformsCopy) {
        co_await platformImpl->clearExpired();
    }
    co_return;
}



drogon::Task<UEAdminAPI::utils::HttpResult> ThirdPartyLoginService::GetLoginUrl(const std::string &platform) {
    UEAdminAPI::utils::HttpResult result;
    if (platform.empty()) {
        result.setResult(ApiErrorCode::ApiError_UnsupportedPlatform, "请指定平台.");
        co_return result;
    }

    auto platformService = co_await getPlatform(platform);

    if (!platformService) {
        result.setResult(ApiErrorCode::ApiError_UnsupportedPlatform);
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
        result.setResult(ApiErrorCode::ApiError_UnsupportedPlatform);
        co_return result;
    }
    if (!co_await platformService->callBack(code, state)) {
        result.setResult(ApiErrorCode::ApiError_ThirdPartyCallbackFailed, "处理第三方登录回调失败, 可能是登录操作超时");
        co_return result;
    }
    if (!co_await platformService->getLoginValue(state)) {
        result.setResult(ApiErrorCode::ApiError_LoginValueNotFound);
        co_return result;
    }
    co_return result;
}

Task<HttpResponsePtr> ThirdPartyLoginService::CallbackRedirect(const std::string &platform, const std::string &code, const std::string &state) {
    auto platformService = co_await getPlatform(platform);

    if (platformService) {
        co_await platformService->callBack(code, state);
    }

    // 构造自定义协议地址
    std::string customProtocol = "ueloginreturn://success?state=" + state;

    // 构造简洁美观的提示页面
    std::string htmlContent =
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "    <meta charset=\"UTF-8\">"
        "    <title>登录跳转中...</title>"
        "    "
        "    <meta http-equiv=\"refresh\" content=\"0;url=" + customProtocol + "\">"
        "    <style>"
        "        body { font-family: 'Microsoft YaHei', sans-serif; display: flex; align-items: center; justify-content: center; height: 100vh; margin: 0; background-color: #f5f5f5; }"
        "        .card { background: white; padding: 40px; border-radius: 8px; box-shadow: 0 4px 12px rgba(0,0,0,0.1); text-align: center; max-width: 400px; }"
        "        .icon { color: #52c41a; font-size: 48px; margin-bottom: 20px; }"
        "        .btn { display: inline-block; margin-top: 20px; padding: 10px 20px; background: #1890ff; color: white; text-decoration: none; border-radius: 4px; }"
        "    </style>"
        "</head>"
        "<body>"
        "    <div class=\"card\">"
        "        <div class=\"icon\">✔</div>"
        "        <h2 style=\"margin:0 0 10px 0;\">验证成功</h2>"
        "        <p style=\"color: #666;\">正在为您跳转回应用，请稍候...</p>"
        "        <p style=\"font-size: 14px; color: #999;\">如果您的应用没有自动弹出，请点击下方按钮：</p>"
        "        <a href=\"" + customProtocol + "\" class=\"btn\">返回应用</a>"
        "        <hr style=\"border:none; border-top:1px solid #eee; margin:20px 0;\">"
        "        <p style=\"color: #999; font-size: 12px;\">现在您可以安全地关闭此浏览器窗口</p>"
        "    </div>"
        "    <script>"
        "        // 第二重保险：JS 跳转"
        "        window.location.href = '" + customProtocol + "';"
        "    </script>"
        "</body>"
        "</html>";

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    resp->setContentTypeCode(CT_TEXT_HTML);
    resp->setBody(std::move(htmlContent));

    co_return resp;

    //resp->addHeader("Location", "ueloginreturn://");
    //co_return resp;
}

Task<HttpResult> ThirdPartyLoginService::BindAccount(int userId, const std::string &platform, const std::string &code, const std::string &verifyCode) {
    auto _authService = AuthService::Instance();

    HttpResult result;
    
    if (platform.empty() || code.empty() || verifyCode.empty()) {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs);
        co_return result;
    }
    
    auto dbClientPtr = drogon::app().getDbClient();
    Mapper<User> mapperUser(dbClientPtr);
    Mapper<UserThirdPartyInfo> mapperThirdPartyInfo(dbClientPtr);
    Mapper<ThirdPartyPlatforms> mapperThirdPartyPlatforms(dbClientPtr);

    // 验证登录
    result = co_await VerifyLogin(platform, code, verifyCode);
    if(result.code != 0){
        result.setResult(ApiErrorCode::ApiError_ThirdPartyAuthFailed);
        co_return result;
    }

    if(result.jsondata["allready_bind"] == true){
        result.setResult(ApiErrorCode::ApiError_PlatformAlreadyBound);
        co_return result;
    }
    
    User targetUser;
    try {
        targetUser = mapperUser.findOne(Criteria(User::Cols::_id, CompareOperator::EQ, userId));
    } catch (const drogon::orm::DrogonDbException &ex) {
        result.setResult(ApiErrorCode::ApiError_InternalError);
        co_return result;
    }
    auto thirdPartyPlatform = co_await getPlatform(platform);
    if (!thirdPartyPlatform) {
        result.setResult(ApiErrorCode::ApiError_UnsupportedPlatform);
        co_return result;
    }
    if (!(co_await thirdPartyPlatform->verifyTheCode(code, verifyCode))) {
        result.setResult(ApiErrorCode::ApiError_InvalidVerifyCode);
        co_return result;
    }
    auto thirdPartyInfos = targetUser.getThird_party_platforms(dbClientPtr);
    auto targetInfos = std::find_if(thirdPartyInfos.begin(), thirdPartyInfos.end(), [thirdPartyPlatform](const std::pair<ThirdPartyPlatforms, UserThirdPartyInfo> &info){
        return info.first.getValueOfPlatformName() == ThirdPartyPlatformToString(thirdPartyPlatform->getPlatform());
    });
    if (targetInfos != thirdPartyInfos.end()) {
        result.setResult(ApiErrorCode::ApiError_PlatformAlreadyBound);
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
        LOG_ERROR << "绑定第三方账号失败: " << ex.base().what();
        result.setResult(ApiErrorCode::ApiError_BindingFailed);
        co_return result;
    }
    result.setResult(ApiErrorCode::ApiError_Success, "绑定成功");
    co_await thirdPartyPlatform->consumeLoginValue(code);
    co_return result;
}

drogon::Task<UEAdminAPI::utils::HttpResult> ThirdPartyLoginService::VerifyLogin(const std::string &platform, const std::string &code, const std::string &verifyCode, bool onlyCheck) {
    auto _authService = AuthService::Instance();
    
    UEAdminAPI::utils::HttpResult result;

    // 检查参数, 并提示缺少的参数
    std::vector<std::string> missingParam;
    if (platform.empty()) {
        missingParam.push_back("platform");
    }
    if (code.empty()) {
        missingParam.push_back("code");
    }
    if (verifyCode.empty()) {
        missingParam.push_back("verifyCode");
    }
    if (!missingParam.empty()) {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "缺少必要参数: " + std::accumulate(missingParam.begin(), missingParam.end(), std::string(), 
            [](const std::string &a, const std::string &b) { return a.empty() ? b : a + ", " + b; }));
        co_return result;
    }

    auto platformService = co_await getPlatform(platform);
    if (!platformService) {
        result.setResult(ApiErrorCode::ApiError_UnsupportedPlatform);
        co_return result;
    }
    bool success = co_await platformService->verifyTheCode(code, verifyCode);
    if (!success) {
        result.setResult(ApiErrorCode::ApiError_InvalidVerifyCode, "验证失败");
        co_return result;
    }
    auto loginValue = co_await platformService->getLoginValue(code);
    if (!loginValue) {
        result.setResult(ApiErrorCode::ApiError_LoginValueNotFound);
        co_return result;
    }

    {
        std::lock_guard<std::recursive_mutex> lock(loginValue->mutex);
        if (loginValue->authorizationCode.empty()) {
            result.setResult(ApiErrorCode::ApiError_CodeNotLoggedIn, "该code尚未登录, 验证失败");
            co_return result;
        }
        if (!loginValue->ready) {
            result.setResult(ApiErrorCode::ApiError_LoginProcessing, "登录请求处理中, 请稍后...");
            co_return result;
        }
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
    result.setResult(ApiErrorCode::ApiError_Success, "验证成功");
    result.jsondata["allready_bind"] = isAllreadyBind;

    // 如果只是检查第三方登录是否已经完成, 则直接返回
    if (onlyCheck) {
        co_return result;
    }

    // 如果不是确认登录, 则需要根据isAllreadyBind来判断是否需要已经绑定
    // 如果绑定则登录, 否则返回提示信息
    if (!isAllreadyBind) {
        result.setResult(ApiErrorCode::ApiError_PlatformNotBound, "该平台未绑定过该账号");
        co_return result;
    }

    // 如果绑定了, 则登录
    result = co_await _authService->LoginByUserId(thirdPartyInfo.getValueOfUserId());
    co_await platformService->consumeLoginValue(code);

    co_return result;
}

drogon::Task<UEAdminAPI::utils::HttpResult> ThirdPartyLoginService::CreateUserFromThirdParty(const std::string &platform, const std::string &code, const std::string &verifyCode) {
    auto _authService = AuthService::Instance();

    UEAdminAPI::utils::HttpResult result;
    auto platformService = co_await getPlatform(platform);
    if (!platformService) {
        result.setResult(ApiErrorCode::ApiError_UnsupportedPlatform);
        co_return result;
    }
    if (code.empty() || verifyCode.empty()) {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "缺少必要参数, code 和 verifyCode");
        co_return result;
    }
    bool success = co_await platformService->verifyTheCode(code, verifyCode);
    if (!success) {
        result.setResult(ApiErrorCode::ApiError_InvalidVerifyCode, "验证失败");
        co_return result;
    }

    // 检查第三方登陆是否已经完成
    result = co_await VerifyLogin(platform, code, verifyCode, true);

    // 检查第三方是否已经绑定账号
    if (result.jsondata["allready_bind"].asBool()) {
        result.setResult(ApiErrorCode::ApiError_PlatformAlreadyBound, "该账号已经被绑定");
        co_return result;
    }

    // 上面已经检查了loginValue 所以不再检查
    auto loginValue = co_await platformService->getLoginValue(code);

    // 新用户创建
    std::string username = "NewUser_" + RandomGenerator::getRandNumberStr(8);
    std::string password = RandomGenerator::generateRandomPassword();
    std::string fakeEmail = username + "@example.com";

    auto [hash, salt] = _authService->CreateStrPasswordHash(password);
    User user;
    user.setName(username);
    user.setNickName(username);
    user.setPasswordHash(hash);
    user.setPasswordSalt(salt);
    user.setCreateAt(trantor::Date::now());
    user.setIsMale(true);
    user.setPrivilege(int(UserPrivileges::User));
    result = co_await _authService->ExecuteRegistrationTransaction(user, password, fakeEmail);

    if (result.code != 0) {
        co_return result;
    }

    auto dbClientPtr = drogon::app().getDbClient();
    Mapper<UserThirdPartyInfo> mapperThirdPartyInfo(dbClientPtr);

    UserThirdPartyInfo thirdPartyInfo;
    thirdPartyInfo.setUserId(user.getValueOfId()); 
    thirdPartyInfo.setPlatformId(int(platformService->getPlatform()));
    thirdPartyInfo.setOpenId(loginValue->openId);
    thirdPartyInfo.setAccessToken(loginValue->accessToken);
    thirdPartyInfo.setNickName(loginValue->nickName);
    thirdPartyInfo.setAvatarImgUrl(loginValue->avatarImgUrl);
    bool isThirdPartyInfoInsertFailed = false;
    
    try {
        mapperThirdPartyInfo.insert(thirdPartyInfo);
    } catch (const drogon::orm::DrogonDbException &e) {
        LOG_ERROR << "创建第三方登录信息失败: " << e.base().what();
        isThirdPartyInfoInsertFailed = true;
    }
    if (isThirdPartyInfoInsertFailed) {
        result = co_await _authService->DeleteUserForce(user.getValueOfId());
        if (result.code != 0) {
            LOG_ERROR << "回滚删除用户时失败: " << result.msg;
            co_return result;
        }
        result.setResult(ApiErrorCode::ApiError_ThirdPartyInfoCreationFailure, "创建第三方登录信息失败, 先前创建的用户已删除");
        co_return result;
    }

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
        result.setResult(ApiErrorCode::ApiError_PlatformNotBound, "该第三方账号未绑定任何用户");
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
        result.setResult(ApiErrorCode::ApiError_InternalError, "内部错误");
        LOG_ERROR << "内部数据不一致, 提示已绑定但找不到对应的数据库记录";
        co_return result;
    }

    // 5. 调用AuthService完成登录
    auto authService = AuthService::Instance();
    result = co_await authService->LoginByUserId(info.getValueOfUserId());
    co_await platformService->consumeLoginValue(code);
    
    co_return result;
}

drogon::Task<UEAdminAPI::utils::HttpResult> ThirdPartyLoginService::UnbindAccount(int userId, const std::string &platform) {
    auto _authService = AuthService::Instance();

    UEAdminAPI::utils::HttpResult result;

    if (platform.empty()) {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "缺少参数 platform");
        co_return result;
    }

    auto platformService = co_await getPlatform(platform);
    if (!platformService) {
        result.setResult(ApiErrorCode::ApiError_UnsupportedPlatform);
        co_return result;
    }

    auto dbClientPtr = drogon::app().getDbClient();
    Mapper<UserThirdPartyInfo> mapperThirdPartyInfo(dbClientPtr);

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
            result.setResult(ApiErrorCode::ApiError_InternalError);
            co_return result;
        }
    }

    if (notBound) {
        result.setResult(ApiErrorCode::ApiError_PlatformNotBound);
        co_return result;
    }

    result.setResult(ApiErrorCode::ApiError_Success, "解绑成功");
    co_return result;
}

} // namespace Services
} // namespace UEAdminAPI

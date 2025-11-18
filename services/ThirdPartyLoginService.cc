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
ThirdPartyLoginPlatformBase::ThirdPartyLoginPlatformBase(const Json::Value& config, const std::string& name)
    : name(name) {
    // 检查并获取配置
    auto [serverHost, clientId, clientSecret] = checkAndGetPlatformConfig(name, config);
    this->serverHost = serverHost;
    this->clientId = clientId;
    this->clientSecret = clientSecret;

    // 设置重定向URL
    this->redirectUrl = this->serverHost + "/api/third/" + DataFormatUtil::toLowerCase(name);

    // 创建HTTP客户端
    httpClient = drogon::HttpClient::newHttpClient("https://graph.qq.com");
}

std::tuple<std::string, std::string, std::string> 
ThirdPartyLoginPlatformBase::checkAndGetPlatformConfig(const std::string& platformName, const Json::Value& config) {
    // 检查配置
    bool isNecessaryConfigsNotSet = false;

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

drogon::Task<ThirdPartyLoginValue*> ThirdPartyLoginPlatformBase::getLoginValue(const std::string& code) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto it = std::find_if(loginValues.begin(), loginValues.end(),
                          [&code](const std::shared_ptr<ThirdPartyLoginValue>& value) {
                              return value->code == code;
                          });
    co_return it != loginValues.end() ? it->get() : nullptr;
}

drogon::Task<bool> ThirdPartyLoginPlatformBase::verifyCode(const std::string& code, const std::string& verifyCode) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
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

    co_return value->verifyCode == verifyCode && !value->authorizationCode.empty();
}

drogon::Task<std::shared_ptr<ThirdPartyLoginValue>> ThirdPartyLoginPlatformBase::createNewThirdLoginValue() {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    co_await clearExpired();

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
    std::lock_guard<std::recursive_mutex> lock(mutex);
    loginValues.erase(
        std::remove_if(loginValues.begin(), loginValues.end(),
                      [](const std::shared_ptr<ThirdPartyLoginValue>& value) {
                          return value->isExpired();
                      }),
        loginValues.end());
    co_return;
}

// ThirdPartyLoginPlatform_QQ 实现
ThirdPartyLoginPlatform_QQ::ThirdPartyLoginPlatform_QQ(const Json::Value& config)
    : ThirdPartyLoginPlatformBase(config, "QQ") {
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
        value->headImgUrl = json["figureurl_qq_2"].asString();
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
        std::lock_guard<std::recursive_mutex> lock(mutex);
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
    std::string accessToken = co_await getAccessToken(targetValue);
    std::string openId = co_await getOpenId(targetValue);
    std::string nickName = co_await getThirdPartyUserNickName(targetValue);
    
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
    : ThirdPartyLoginPlatformBase(config, "WeChat") {
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
       << "&code=" << value->code
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
        value->headImgUrl = json["headimgurl"].asString();
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
        std::lock_guard<std::recursive_mutex> lock(mutex);
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
    platforms[ThirdPartyPlatform::QQ] = std::make_unique<ThirdPartyLoginPlatform_QQ>(config);

    // 添加微信平台
    platforms[ThirdPartyPlatform::WeChat] = std::make_unique<ThirdPartyLoginPlatform_WeChat>(config);
}

drogon::Task<IThirdPartyLoginPlatform*> ThirdPartyLoginService::getPlatform(ThirdPartyPlatform platform) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = platforms.find(platform);
    co_return it != platforms.end() ? it->second.get() : nullptr;
}

drogon::Task<void> ThirdPartyLoginService::deletePlatform(ThirdPartyPlatform platform) {
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

} // namespace Services
} // namespace UEAdminAPI

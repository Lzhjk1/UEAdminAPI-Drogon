#include "ThirdPartyLoginService.h"
#include "utils/RandomGenerator.h"
#include <drogon/HttpClient.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>
#include <iostream>
#include <sstream>
#include <algorithm>

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
    this->redirectUrl = this->serverHost + "/api/third/" + name;

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

ThirdPartyLoginValue* ThirdPartyLoginPlatformBase::getLoginValue(const std::string& code) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto it = std::find_if(loginValues.begin(), loginValues.end(),
                          [&code](const std::shared_ptr<ThirdPartyLoginValue>& value) {
                              return value->code == code;
                          });
    return it != loginValues.end() ? it->get() : nullptr;
}

bool ThirdPartyLoginPlatformBase::verifyCode(const std::string& code, const std::string& verifyCode) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto it = std::find_if(loginValues.begin(), loginValues.end(),
                          [&code](const std::shared_ptr<ThirdPartyLoginValue>& value) {
                              return value->code == code;
                          });

    if (it == loginValues.end()) {
        return false;
    }

    auto& value = *it;
    if (value->isExpired()) {
        loginValues.erase(it);
        return false;
    }

    return value->verifyCode == verifyCode;
}

std::shared_ptr<ThirdPartyLoginValue> ThirdPartyLoginPlatformBase::createNewThirdLoginValue() {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    clearExpired();

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
    return newLoginValue;
}

void ThirdPartyLoginPlatformBase::clearExpired() {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    loginValues.erase(
        std::remove_if(loginValues.begin(), loginValues.end(),
                      [](const std::shared_ptr<ThirdPartyLoginValue>& value) {
                          return value->isExpired();
                      }),
        loginValues.end());
}

// ThirdPartyLoginPlatform_QQ 实现
ThirdPartyLoginPlatform_QQ::ThirdPartyLoginPlatform_QQ(const Json::Value& config)
    : ThirdPartyLoginPlatformBase(config, "QQ") {
    // QQ使用特定的客户端
    httpClient = drogon::HttpClient::newHttpClient("https://graph.qq.com");
}

bool ThirdPartyLoginPlatform_QQ::fetchTokens(std::shared_ptr<ThirdPartyLoginValue> value) {
    if (value->code.empty()) {
        throw std::runtime_error("登录码为空, 请检查登录码是否正确.");
    }

    // 构造请求链接
    std::stringstream ss;
    ss << "https://graph.qq.com/oauth2.0/token?grant_type=authorization_code"
       << "&client_id=" << clientId
       << "&client_secret=" << clientSecret
       << "&code=" << value->code
       << "&redirect_uri=" << redirectUrl
       << "&fmt=json";

    // 发送请求
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath(ss.str());

    auto [result, response] = httpClient->sendRequest(req);
    
    
    if (result != drogon::ReqResult::Ok) {
        LOG_ERROR << "QQ平台获取AccessToken失败, 无法获取响应";
        return false;
    }

    // 处理响应
    if (response->getStatusCode() != drogon::k200OK) {
        LOG_ERROR << "QQ平台获取AccessToken失败, 状态码: " << response->getStatusCode()
                  << ", 原因: " << response->getJsonError();
        return false;
    }

    // 解析JSON
    Json::Value json;
    Json::Reader reader;
    if (!reader.parse(std::string(response->getBody()), json)) {
        LOG_ERROR << "QQ平台获取AccessToken失败, 解析响应为JSON时失败, 响应原文: " << response->getBody();
        return false;
    }

    // 处理业务错误
    if (json.isMember("ret") && json["ret"].asInt() != 0) {
        LOG_ERROR << "QQ平台获取AccessToken失败, 错误码: " << json["ret"].asInt()
                  << ", 错误信息: " << json["msg"].asString();
        return false;
    }

    // 保存数据到Value
    if (json.isMember("access_token")) {
        value->accessToken = json["access_token"].asString();
    }
    if (json.isMember("refresh_token")) {
        value->refreshToken = json["refresh_token"].asString();
    }

    return true;
}

std::string ThirdPartyLoginPlatform_QQ::getAccessToken(std::shared_ptr<ThirdPartyLoginValue> value) {
    if (value->accessToken.empty()) {
        if (!fetchTokens(value)) {
            LOG_ERROR << "获取QQ平台AccessToken失败.";
            return "";
        }
    }
    if (value->accessToken.empty()) {
        LOG_ERROR << "获取QQ平台AccessToken失败, FetchTokens后AccessToken仍为空.";
        return "";
    }

    return value->accessToken;
}

bool ThirdPartyLoginPlatform_QQ::fetchOpenId(std::shared_ptr<ThirdPartyLoginValue> value) {
    std::string accessToken = value->accessToken;
    if (accessToken.empty()) {
        LOG_ERROR << "获取QQ平台OpenId失败, AccessToken为空.";
        return false;
    }

    // 构造请求链接
    std::stringstream ss;
    ss << "https://graph.qq.com/oauth2.0/me?access_token=" << accessToken << "&fmt=json";

    // 发送请求
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath(ss.str());

    auto [result, response] = httpClient->sendRequest(req);
    if (!response) {
        LOG_ERROR << "QQ平台获取OpenId失败, 无法获取响应";
        return false;
    }

    // 处理响应
    if (response->getStatusCode() != drogon::k200OK) {
        LOG_ERROR << "QQ平台获取OpenId失败, 状态码: " << response->getStatusCode()
                  << ", 原因: " << response->getJsonError();
        return false;
    }

    // 解析JSON
    Json::Value json;
    Json::Reader reader;
    if (!reader.parse(std::string(response->getBody()), json)) {
        LOG_ERROR << "QQ平台获取OpenId失败, 解析响应为JSON时失败, 响应原文: " << response->getBody();
        return false;
    }

    // 处理业务错误
    if (json.isMember("ret") && json["ret"].asInt() != 0) {
        LOG_ERROR << "QQ平台获取OpenId失败, 错误码: " << json["ret"].asInt()
                  << ", 错误信息: " << json["msg"].asString();
        return false;
    }

    // 保存数据到Value
    if (json.isMember("openid")) {
        value->openId = json["openid"].asString();
    }

    return true;
}

std::string ThirdPartyLoginPlatform_QQ::getOpenId(std::shared_ptr<ThirdPartyLoginValue> value) {
    if (!value->openId.empty()) {
        return value->openId;
    }

    std::string accessToken = getAccessToken(value);
    if (accessToken.empty()) {
        LOG_ERROR << "获取QQ平台OpenId失败, AccessToken为空.";
        return "";
    }

    if (!fetchOpenId(value)) {
        LOG_ERROR << "获取QQ平台OpenId失败.";
        return "";
    }

    return value->openId;
}

std::string ThirdPartyLoginPlatform_QQ::getAuthorizationUrl(std::shared_ptr<ThirdPartyLoginValue> value) {
    std::stringstream ss;
    ss << "https://graph.qq.com/oauth2.0/authorize?response_type=code"
       << "&client_id=" << clientId
       << "&redirect_uri=" << redirectUrl
       << "&state=" << value->code;

    return ss.str();
}

bool ThirdPartyLoginPlatform_QQ::fetchThirdPartyUserInfo(std::shared_ptr<ThirdPartyLoginValue> value) {
    std::string accessToken = value->accessToken;
    std::string openId = value->openId;

    if (accessToken.empty() || openId.empty()) {
        LOG_ERROR << "获取QQ平台用户信息失败, AccessToken或OpenId为空.";
        return false;
    }

    // 构造请求链接
    std::stringstream ss;
    ss << "https://graph.qq.com/user/get_user_info"
       << "?access_token=" << accessToken
       << "&oauth_consumer_key=" << clientId
       << "&openid=" << openId
       << "&format=json";

    // 发送请求
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath(ss.str());

    auto [result, response] = httpClient->sendRequest(req);
    if (!response) {
        LOG_ERROR << "QQ平台获取用户信息失败, 无法获取响应";
        return false;
    }

    // 处理响应
    if (response->getStatusCode() != drogon::k200OK) {
        LOG_ERROR << "QQ平台获取用户信息失败, 状态码: " << response->getStatusCode()
                  << ", 原因: " << response->getJsonError();
        return false;
    }

    // 解析JSON
    Json::Value json;
    Json::Reader reader;
    if (!reader.parse(std::string(response->getBody()), json)) {
        LOG_ERROR << "QQ平台获取用户信息失败, 解析响应为JSON时失败, 响应原文: " << response->getBody();
        return false;
    }

    // 处理业务错误
    if (json.isMember("ret") && json["ret"].asInt() != 0) {
        LOG_ERROR << "QQ平台获取用户信息失败, 错误码: " << json["ret"].asInt()
                  << ", 错误信息: " << json["msg"].asString();
        return false;
    }

    // 保存数据到Value
    if (json.isMember("nickname")) {
        value->nickName = json["nickname"].asString();
    }
    if (json.isMember("figureurl_qq_2")) {
        value->headImgUrl = json["figureurl_qq_2"].asString();
    }

    return true;
}

std::string ThirdPartyLoginPlatform_QQ::getThirdPartyUserNickName(std::shared_ptr<ThirdPartyLoginValue> value) {
    if (!value->nickName.empty()) {
        return value->nickName;
    }

    if (!fetchThirdPartyUserInfo(value)) {
        LOG_ERROR << "获取QQ平台用户昵称失败.";
        return "";
    }

    if (value->nickName.empty()) {
        LOG_ERROR << "获取QQ平台用户昵称失败, 获取到的昵称为空.";
        return "";
    }

    return value->nickName;
}

bool ThirdPartyLoginPlatform_QQ::callBack(const std::string& code, const std::string& state) {
    std::shared_ptr<ThirdPartyLoginValue> targetValue;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        auto it = std::find_if(loginValues.begin(), loginValues.end(),
                              [&state](const std::shared_ptr<ThirdPartyLoginValue>& value) {
                                  return value->code == state;
                              });
        if (it == loginValues.end()) {
            LOG_ERROR << "QQ平台回调失败, 找不到对应的登录值, Code: " << code << ", State: " << state;
            return false;
        }
        targetValue = *it;
    }

    targetValue->authorizationCode = code;
    LOG_INFO << "QQ平台回调成功, Code: " << code << ", State: " << state
             << " AccessToken = " << getAccessToken(targetValue)
             << " OpenId = " << getOpenId(targetValue)
             << " NickName = " << getThirdPartyUserNickName(targetValue);

    return true;
}

// ThirdPartyLoginPlatform_WeChat 实现
ThirdPartyLoginPlatform_WeChat::ThirdPartyLoginPlatform_WeChat(const Json::Value& config)
    : ThirdPartyLoginPlatformBase(config, "WeChat") {
    // 微信使用特定的客户端
    httpClient = drogon::HttpClient::newHttpClient("https://api.weixin.qq.com");
}

bool ThirdPartyLoginPlatform_WeChat::fetchTokens(std::shared_ptr<ThirdPartyLoginValue> value) {
    if (value->code.empty()) {
        throw std::runtime_error("登录码为空, 请检查登录码是否正确.");
    }

    // 构造请求链接
    std::stringstream ss;
    ss << "https://api.weixin.qq.com/sns/oauth2/access_token"
       << "?appid=" << clientId
       << "&secret=" << clientSecret
       << "&code=" << value->code
       << "&grant_type=authorization_code";

    // 发送请求
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath(ss.str());

    auto [result, response] = httpClient->sendRequest(req);
    if (!response) {
        LOG_ERROR << "微信平台获取AccessToken失败, 无法获取响应";
        return false;
    }

    // 处理响应
    if (response->getStatusCode() != drogon::k200OK) {
        LOG_ERROR << "微信平台获取AccessToken失败, 状态码: " << response->getStatusCode()
                  << ", 原因: " << response->getJsonError();
        return false;
    }

    // 解析JSON
    Json::Value json;
    Json::Reader reader;
    if (!reader.parse(std::string(response->getBody()), json)) {
        LOG_ERROR << "微信平台获取AccessToken失败, 解析响应为JSON时失败, 响应原文: " << response->getBody();
        return false;
    }

    // 微信比较特殊, 错误时返回另一结构的json数据, 不能直接解析
    if (json.isMember("errcode")) {
        LOG_ERROR << "微信平台获取AccessToken失败, 错误码: " << json["errcode"].asInt()
                  << ", 错误信息: " << json["errmsg"].asString();
        return false;
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

    return true;
}

std::string ThirdPartyLoginPlatform_WeChat::getAccessToken(std::shared_ptr<ThirdPartyLoginValue> value) {
    if (value->accessToken.empty()) {
        if (!fetchTokens(value)) {
            LOG_ERROR << "获取微信平台AccessToken失败.";
            return "";
        }
    }
    if (value->accessToken.empty()) {
        LOG_ERROR << "获取微信平台AccessToken失败, FetchTokens后AccessToken仍为空.";
        return "";
    }

    return value->accessToken;
}

bool ThirdPartyLoginPlatform_WeChat::fetchOpenId(std::shared_ptr<ThirdPartyLoginValue> value) {
    // 微信平台在获取Token的同时也获取了OpenId, 所以不需要单独获取OpenId. 但接口定义了这个方法, 必须要实现
    return fetchTokens(value);
}

std::string ThirdPartyLoginPlatform_WeChat::getOpenId(std::shared_ptr<ThirdPartyLoginValue> value) {
    if (!value->openId.empty()) {
        return value->openId;
    }

    std::string accessToken = getAccessToken(value);
    if (accessToken.empty()) {
        LOG_ERROR << "获取微信平台OpenId失败, AccessToken为空.";
        return "";
    }

    if (!fetchOpenId(value)) {
        LOG_ERROR << "获取微信平台OpenId失败.";
        return "";
    }

    return value->openId;
}

std::string ThirdPartyLoginPlatform_WeChat::getAuthorizationUrl(std::shared_ptr<ThirdPartyLoginValue> value) {
    std::stringstream ss;
    ss << "https://open.weixin.qq.com/connect/qrconnect"
       << "?appid=" << clientId
       << "&redirect_uri=" << redirectUrl
       << "&state=" << value->code
       << "&scope=snsapi_login"
       << "&response_type=code";

    return ss.str();
}

bool ThirdPartyLoginPlatform_WeChat::fetchThirdPartyUserInfo(std::shared_ptr<ThirdPartyLoginValue> value) {
    std::string accessToken = value->accessToken;
    std::string openId = value->openId;

    if (accessToken.empty() || openId.empty()) {
        LOG_ERROR << "获取微信平台用户信息失败, AccessToken或OpenId为空.";
        return false;
    }

    // 构造请求链接
    std::stringstream ss;
    ss << "https://api.weixin.qq.com/sns/userinfo"
       << "?access_token=" << accessToken
       << "&openid=" << openId;

    // 发送请求
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath(ss.str());

    auto [result, response] = httpClient->sendRequest(req);
    if (!response) {
        LOG_ERROR << "微信平台获取用户信息失败, 无法获取响应";
        return false;
    }

    // 处理响应
    if (response->getStatusCode() != drogon::k200OK) {
        LOG_ERROR << "微信平台获取用户信息失败, 状态码: " << response->getStatusCode()
                  << ", 原因: " << response->getJsonError();
        return false;
    }

    // 解析JSON
    Json::Value json;
    Json::Reader reader;
    if (!reader.parse(std::string(response->getBody()), json)) {
        LOG_ERROR << "微信平台获取用户信息失败, 解析响应为JSON时失败, 响应原文: " << response->getBody();
        return false;
    }

    // 处理业务错误
    if (json.isMember("errcode")) {
        LOG_ERROR << "微信平台获取用户信息失败, 错误码: " << json["errcode"].asInt()
                  << ", 错误信息: " << json["errmsg"].asString();
        return false;
    }

    // 保存数据到Value
    if (json.isMember("nickname")) {
        value->nickName = json["nickname"].asString();
    }
    if (json.isMember("headimgurl")) {
        value->headImgUrl = json["headimgurl"].asString();
    }

    return true;
}

std::string ThirdPartyLoginPlatform_WeChat::getThirdPartyUserNickName(std::shared_ptr<ThirdPartyLoginValue> value) {
    if (!value->nickName.empty()) {
        return value->nickName;
    }

    if (!fetchThirdPartyUserInfo(value)) {
        LOG_ERROR << "获取微信平台用户昵称失败.";
        return "";
    }

    if (value->nickName.empty()) {
        LOG_ERROR << "获取微信平台用户昵称失败, 获取到的昵称为空.";
        return "";
    }

    return value->nickName;
}

bool ThirdPartyLoginPlatform_WeChat::callBack(const std::string& code, const std::string& state) {
    std::shared_ptr<ThirdPartyLoginValue> targetValue;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        auto it = std::find_if(loginValues.begin(), loginValues.end(),
                              [&state](const std::shared_ptr<ThirdPartyLoginValue>& value) {
                                  return value->code == state;
                              });
        if (it == loginValues.end()) {
            LOG_ERROR << "微信平台回调失败, 找不到对应的登录值, Code: " << code << ", State: " << state;
            return false;
        }
        targetValue = *it;
    }

    targetValue->authorizationCode = code;
    LOG_INFO << "微信平台回调成功, Code: " << code << ", State: " << state
             << " AccessToken = " << getAccessToken(targetValue)
             << " OpenId = " << getOpenId(targetValue)
             << " NickName = " << getThirdPartyUserNickName(targetValue);

    return true;
}

// ThirdPartyLoginService 实现
ThirdPartyLoginService::ThirdPartyLoginService(const Json::Value& config) {
    // 添加QQ平台
    platforms["QQ"] = std::make_unique<ThirdPartyLoginPlatform_QQ>(config);

    // 添加微信平台
    platforms["WeChat"] = std::make_unique<ThirdPartyLoginPlatform_WeChat>(config);
}

IThirdPartyLoginPlatform* ThirdPartyLoginService::getPlatform(ThirdPartyPlatform platform) {
    std::string platformName;
    switch (platform) {
        case ThirdPartyPlatform::QQ:
            platformName = "QQ";
            break;
        case ThirdPartyPlatform::WeChat:
            platformName = "WeChat";
            break;
        default:
            return nullptr;
    }

    std::lock_guard<std::mutex> lock(mutex);
    auto it = platforms.find(platformName);
    return it != platforms.end() ? it->second.get() : nullptr;
}

void ThirdPartyLoginService::deletePlatform(const std::string& platformName) {
    std::lock_guard<std::mutex> lock(mutex);
    platforms.erase(platformName);
}

void ThirdPartyLoginService::clearExpired() {
    for (auto& [name, platform] : platforms) {
        platform->clearExpired();
    }
}

} // namespace Services
} // namespace UEAdminAPI

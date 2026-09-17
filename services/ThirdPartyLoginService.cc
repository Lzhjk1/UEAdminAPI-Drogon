#include "ThirdPartyLoginService.h"
#include "utils/RandomGenerator.h"
#include <drogon/HttpClient.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpViewData.h>
#include <drogon/utils/Utilities.h>
#include <json/json.h>
#include <iostream>
#include <sstream>
#include <algorithm>
#include "utils/DataFormatUtils.h"
#include <shared_mutex>
#include "models/User.h"
#include "models/ThirdPartyPlatforms.h"
#include "models/UserThirdPartyInfo.h"
#include <drogon/orm/Mapper.h>
#include "services/AuthService.h"
#include "services/DeviceLoginSessionService.h"
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

namespace {

/// @brief 从第三方回调 state 中解析 ueadmin_state 设备会话参数。
/// 平台可能对 state 整体做 URL 编码，因此先解码；随后按 query 参数方式分割，
/// 取 ueadmin_state 的值，避免固定分隔符 + 截取到末尾导致多余参数混入。
struct ParsedUeAdminState {
    std::string thirdPartyState;
    std::string deviceState;
};

ParsedUeAdminState ParseUeAdminState(const std::string &state) {
    ParsedUeAdminState result;
    if (state.empty()) {
        return result;
    }

    // 平台可能把整个 state 作为 query 参数回传（例如 state=xyz&ueadmin_state=abc），
    // 也可能直接把 ueadmin_state 拼在原始 state 后。先做一次 URL 解码统一形态。
    std::string decoded = drogon::utils::urlDecode(state);
    if (decoded.empty()) {
        decoded = state;
    }

    // 优先处理带 ? 的完整回调 URL（形如 http://host/cb?code=xx&state=yy）。
    // 第三方 state 本身通常是一段不包含 '=' 的随机串，因此直接用 find 保持兼容；
    // 若平台把第三方 state 也做成了 key=value 形式，则由下面的参数分割逻辑提取。
    auto markerPos = decoded.find("ueadmin_state=");
    if (markerPos != std::string::npos) {
        size_t valueStart = markerPos + strlen("ueadmin_state=");
        auto valueEnd = decoded.find('&', valueStart);
        if (valueEnd == std::string::npos) {
            valueEnd = decoded.size();
        }
        result.deviceState = drogon::utils::urlDecode(decoded.substr(valueStart, valueEnd - valueStart));
        // 原始第三方 state = ueadmin_state 之前的部分；若该部分是完整 URL，保留其 ?state= 形态。
        result.thirdPartyState = decoded.substr(0, markerPos);
        // 兼容 “ueadmin_state=xxx” 位于字符串最前（旧格式没有第三方 state）的情况。
        if (result.thirdPartyState.empty()) {
            result.thirdPartyState = decoded;
        }
        return result;
    }

    // 兼容“第三方 state 被平台编码成 key=value 参数”的场景：
    // 按 & 分割，取 ueadmin_state 值，并还原 ueadmin_state 之前的原始第三方 state。
    std::string query = decoded;
    auto qPos = query.find('?');
    if (qPos != std::string::npos) {
        query = query.substr(qPos + 1);
    }

    size_t start = 0;
    while (start <= query.size()) {
        auto ampPos = query.find('&', start);
        if (ampPos == std::string::npos) {
            ampPos = query.size();
        }
        std::string param = query.substr(start, ampPos - start);
        if (!param.empty()) {
            auto eqPos = param.find('=');
            std::string key = eqPos == std::string::npos ? param : param.substr(0, eqPos);
            std::string value = eqPos == std::string::npos ? "" : param.substr(eqPos + 1);
            if (key == "ueadmin_state") {
                result.deviceState = drogon::utils::urlDecode(value);
            } else if (result.thirdPartyState.empty() && key == "state") {
                // 只认标准 OAuth 的 state 参数，避免把 redirect_uri 等参数误当第三方 state。
                result.thirdPartyState = drogon::utils::urlDecode(value);
            }
        }
        if (ampPos == query.size()) {
            break;
        }
        start = ampPos + 1;
    }

    if (result.deviceState.empty()) {
        result.thirdPartyState = decoded;
    }
    return result;
}

/// @brief 按 (open_id, platform_id) 反查已绑定的本地用户 ID；未绑定（或无 openId）返回 -1。
/// 设备登录回调里"该第三方账号绑没绑"要判断两次（注册前、注册后各一次），故抽出来。
int FindBoundUserId(const std::string &openId, int platformId) {
    if (openId.empty()) {
        return -1;
    }
    auto dbClientPtr = drogon::app().getDbClient();
    Mapper<UserThirdPartyInfo> mapperThirdPartyInfo(dbClientPtr);
    try {
        auto info = mapperThirdPartyInfo.findOne(
            Criteria(UserThirdPartyInfo::Cols::_open_id, CompareOperator::EQ, openId) &&
            Criteria(UserThirdPartyInfo::Cols::_platform_id, CompareOperator::EQ, platformId));
        return info.getValueOfUserId();
    } catch (const drogon::orm::UnexpectedRows &) {
        return -1;
    }
}

} // namespace

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

    // 关键配置未设置时，记录警告并返回空元组（该平台被禁用）
    if (isNecessaryConfigsNotSet) {
        LOG_WARN << "关键配置未设置, 第三方平台 '" << platformName << "' 将被禁用. 请检查配置文件.";
        return std::make_tuple(std::string(), std::string(), std::string());
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

    auto value = *it;
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

    LOG_INFO << "ThirdPartyLoginService 初始化完成";
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
    std::vector<std::pair<UEAdminAPI::utils::EnumThirdPartyPlatform, IThirdPartyLoginPlatform*>> platformsCopy;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for(const auto& pair : platforms) {
            platformsCopy.push_back({pair.first, pair.second.get()});
        }
    }

    for(const auto& platform: platformsCopy) {
        auto value = co_await platform.second->getLoginValue(code);
        if(!value){
            continue;
        }
        co_return std::make_tuple(platform.first, value);
    }
    
    co_return std::make_tuple(UEAdminAPI::utils::EnumThirdPartyPlatform::None, nullptr);
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



drogon::Task<UEAdminAPI::utils::HttpResult> ThirdPartyLoginService::GetLoginUrl(const std::string &platform, const std::string &deviceState) {
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

    // 若调用方是设备登录页（OAuth2 login/start 会话），记下设备 state 以便回调时定位
    // loopback redirect_uri。
    // 注意：只能存在服务端（loginValue->deviceState），不能追加到 authUrl 上 ——
    // 第三方平台回调只会带回 code 和 state（redirect_uri 也是固定值），任何附加的
    // 自定义 query 参数都会被平台丢弃，回调时按 code 反查才是可靠的做法。
    if (!deviceState.empty()) {
        loginValue->deviceState = deviceState;
    }

    result.jsondata["code"] = loginValue->code;
    result.jsondata["verifyCode"] = loginValue->verifyCode;
    result.jsondata["authorizationUrl"] = authUrl;
    result.jsondata["state"] = deviceState;
    // appid / redirectUri: 供登录页在页内实例化微信 wxLogin.js 渲染二维码
    result.jsondata["appId"] = platformService->getClientId();
    result.jsondata["redirectUri"] = platformService->getRedirectUrl();
    co_return result;
}

drogon::Task<UEAdminAPI::utils::HttpResult> ThirdPartyLoginService::Callback(const std::string &platform, const std::string &code, const std::string &state) {
    auto platformService = co_await getPlatform(platform);

    UEAdminAPI::utils::HttpResult result;

    if (!platformService) {
        result.setResult(ApiErrorCode::ApiError_UnsupportedPlatform);
        co_return result;
    }

    // 与 CallbackRedirect 保持一致：剥离 ueadmin_state 后再按第三方原始 state 处理回调。
    // 平台可能对 state 整体做 URL 编码，因此先解码再按 query 参数方式解析。
    auto parsed = ParseUeAdminState(state);
    std::string thirdPartyState = parsed.thirdPartyState;

    if (!co_await platformService->callBack(code, thirdPartyState)) {
        result.setResult(ApiErrorCode::ApiError_ThirdPartyCallbackFailed, "处理第三方登录回调失败, 可能是登录操作超时");
        co_return result;
    }
    if (!co_await platformService->getLoginValue(thirdPartyState)) {
        result.setResult(ApiErrorCode::ApiError_LoginValueNotFound);
        co_return result;
    }
    co_return result;
}

Task<HttpResponsePtr> ThirdPartyLoginService::CallbackRedirect(const std::string &platform, const std::string &code, const std::string &state) {
    auto platformService = co_await getPlatform(platform);

    auto parsed = ParseUeAdminState(state);
    std::string thirdPartyState = parsed.thirdPartyState;
    std::string deviceState = parsed.deviceState;

    // 按第三方 code 反查本次登录记录 —— 由设备登录页发起的登录把设备 state 存在那里
    // （见 GetLoginUrl）。parsed.deviceState 只在旧格式（state 里夹带参数）下非空，保留作兼容回退。
    std::shared_ptr<ThirdPartyLoginValue> loginValue;
    if (platformService) {
        loginValue = co_await platformService->getLoginValue(thirdPartyState);
        if (deviceState.empty() && loginValue && !loginValue->deviceState.empty()) {
            deviceState = loginValue->deviceState;
        }
        // state 可能携带 ueadmin_state，必须把原始第三方 state 还原后再交给平台回调
        co_await platformService->callBack(code, thirdPartyState);
        // callBack 会把 openId 等写回同一个对象 (shared_ptr)，因此上面拿到的 loginValue 仍然有效
    }

    // 由设备登录页发起时，回调后要跳转到该设备会话保存的 loopback redirect_uri（仅通知，不传 token）。
    auto sessionService = UEAdminAPI::Services::DeviceLoginSessionService::Instance();
    if (!deviceState.empty() && sessionService) {
        auto session = sessionService->FindSession(deviceState);
        if (session && !session->redirectUri.empty()) {
            // 把 userId 写回设备会话后，/api/oauth2/login/check 才能返回 token/flashToken。
            // 顺序：已绑定则直接用；没绑定则按 /api/third/register 的做法快速注册并绑定，
            // 免得用户扫码后卡在"验证成功但客户端登录不上"。
            const int platformId = int(platformService->getPlatform());
            bool loggedIn = session->userId > 0;
            std::string regError;
            if (!loggedIn && loginValue && !loginValue->openId.empty()) {
                int userId = FindBoundUserId(loginValue->openId, platformId);
                if (userId <= 0) {
                    auto regResult = co_await CreateUserFromThirdParty(platform, thirdPartyState, loginValue->verifyCode);
                    if (regResult.code == 0) {
                        LOG_INFO << "设备登录第三方回调: 该第三方账号未绑定, 已自动注册并绑定新账号, deviceState=" << deviceState;
                    } else if (regResult.code == ApiErrorCode::ApiError_PlatformAlreadyBound) {
                        // 并发下（比如同一二维码被扫两次）可能刚好已被另一个请求绑定，按已绑定继续
                        LOG_WARN << "设备登录第三方回调: 自动注册时发现已被绑定, 按已绑定继续, deviceState=" << deviceState;
                    } else {
                        regError = regResult.msg;
                        LOG_WARN << "设备登录第三方回调: 自动注册失败: " << regResult.msg
                                 << ", deviceState=" << deviceState;
                    }
                    // 注册成功或已被绑定，这里都能查到 userId
                    userId = FindBoundUserId(loginValue->openId, platformId);
                }
                if (userId > 0) {
                    loggedIn = sessionService->MarkLoggedIn(deviceState, userId);
                } else {
                    LOG_WARN << "设备登录第三方回调: 未能定位到本地用户, 不标记设备会话 userId, deviceState=" << deviceState;
                }
            }

            if (!loggedIn) {
                // 兜底：绝不能跳 loopback —— 否则浏览器显示"验证成功"而客户端永远等不到 token。
                // 渲染提示页说明原因，本次设备会话按超时自然结束。
                std::string platformLabel;
                switch (getPlatformFromString(platform)) {
                    case EnumThirdPartyPlatform::WeChat: platformLabel = "微信"; break;
                    case EnumThirdPartyPlatform::QQ:     platformLabel = "QQ";   break;
                    default:                             platformLabel = platform; break;
                }
                std::string reason;
                if (!loginValue || loginValue->openId.empty()) {
                    // 回调本身没走完（记录过期、授权被取消等），与"注册/绑定失败"是两回事，文案要分开
                    reason = platformLabel + "登录未完成或已过期，请回到客户端重新发起登录。";
                } else {
                    reason = "无法用该" + platformLabel + "账号登录客户端（自动注册账号未成功）。"
                             "请回到客户端重新扫码，或联系管理员。";
                    if (!regError.empty()) {
                        reason += " 原因：" + regError;
                    }
                }
                HttpViewData unboundData;
                unboundData.insertAsString("reason", HttpViewData::htmlTranslate(reason));
                auto resp = HttpResponse::newHttpViewResponse("third_party_unbound.csp", unboundData);
                co_return resp;
            }

            std::string customProtocol = session->redirectUri;
            HttpViewData viewData;
            viewData.insert("customProtocol", customProtocol);
            auto resp = HttpResponse::newHttpViewResponse("login_redirect.csp", viewData);
            co_return resp;
        }
    }

    // 构造自定义协议地址（兼容 ueclient / 非设备登录流程）
    std::string customProtocol = "ueloginreturn://success?state=" + thirdPartyState;

    // 渲染登录跳转提示页 (views/login_redirect.csp, 编译期已嵌入 exe)
    HttpViewData viewData;
    viewData.insert("customProtocol", customProtocol);

    auto resp = HttpResponse::newHttpViewResponse("login_redirect.csp", viewData);

    co_return resp;
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

    result = co_await _authService->LoginByUserId(user.getValueOfId());

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

#pragma once
#include "utils/SingletonWithInit.h"
#include "json/json.h"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <drogon/HttpClient.h>
#include <drogon/HttpAppFramework.h>

namespace UEAdminAPI {
namespace Services {

// 第三方平台枚举
enum class ThirdPartyPlatform {
    QQ,
    WeChat,
    None    // 解析出错时使用的错误值, 不要把它与一个平台相对应
};
std::string ThirdPartyPlatformToString(ThirdPartyPlatform platform, bool isLowerCase = false);

// 第三方登录值
class ThirdPartyLoginValue {
public:
    ThirdPartyLoginValue(const std::string& code, const std::string& verifyCode, 
                        const std::chrono::system_clock::time_point& expireTime);

    ThirdPartyLoginValue(const std::string& code, const std::string& verifyCode);


    std::string code;
    std::string verifyCode;
    std::string authorizationCode;  // 回调时获得的code, 用于获取accessToken
    std::string accessToken;        // QQ官方给出60天有效期, 用于获取OpenID等信息
    std::string refreshToken;
    std::string openId;
    std::string nickName;
    std::string headImgUrl;
    std::chrono::system_clock::time_point expireTime;

    bool isExpired() const {
        return expireTime <= std::chrono::system_clock::now();
    }
};

// 第三方登录平台接口
class IThirdPartyLoginPlatform {
public:
    virtual ~IThirdPartyLoginPlatform() = default;

    // 获取code对应的登录值
    virtual drogon::Task<ThirdPartyLoginValue*> getLoginValue(const std::string& code) = 0;
    // 验证code与verifyCode是否匹配, 并且验证是否已经经过callback获取了authorizationCode
    virtual drogon::Task<bool> verifyCode(const std::string& code, const std::string& verifyCode) = 0;
    virtual drogon::Task<std::shared_ptr<ThirdPartyLoginValue>> createNewThirdLoginValue() = 0;
    virtual drogon::Task<void> clearExpired() = 0;
    virtual drogon::Task<bool> fetchTokens(std::shared_ptr<ThirdPartyLoginValue> value) = 0;
    virtual drogon::Task<std::string> getAccessToken(std::shared_ptr<ThirdPartyLoginValue> value) = 0;
    virtual drogon::Task<bool> fetchOpenId(std::shared_ptr<ThirdPartyLoginValue> value) = 0;
    virtual drogon::Task<std::string> getOpenId(std::shared_ptr<ThirdPartyLoginValue> value) = 0;
    virtual drogon::Task<std::string> getAuthorizationUrl(std::shared_ptr<ThirdPartyLoginValue> value) = 0;
    virtual drogon::Task<bool> fetchThirdPartyUserInfo(std::shared_ptr<ThirdPartyLoginValue> value) = 0;
    virtual drogon::Task<std::string> getThirdPartyUserNickName(std::shared_ptr<ThirdPartyLoginValue> value) = 0;
    virtual drogon::Task<bool> callBack(const std::string& code, const std::string& state) = 0;
    virtual drogon::Task<std::string> saveInfoToDB(std::shared_ptr<ThirdPartyLoginValue> value) = 0;

    virtual ThirdPartyPlatform getPlatform() const = 0;
    virtual std::string getRedirectUrl() const = 0;
};

// 第三方登录平台基类
class ThirdPartyLoginPlatformBase : public IThirdPartyLoginPlatform {
public:
    ThirdPartyLoginPlatformBase(const Json::Value& config, const ThirdPartyPlatform& platform);
    virtual ~ThirdPartyLoginPlatformBase() = default;

    drogon::Task<ThirdPartyLoginValue*> getLoginValue(const std::string& code) override;
    drogon::Task<bool> verifyCode(const std::string& code, const std::string& verifyCode) override;
    drogon::Task<std::shared_ptr<ThirdPartyLoginValue>> createNewThirdLoginValue() override;
    drogon::Task<void> clearExpired() override;

    ThirdPartyPlatform getPlatform() const override {
        return platform;
    }

    std::string getRedirectUrl() const override {
        return redirectUrl;
    }

protected:
    std::string serverHost;
    std::string clientId;
    std::string clientSecret;
    ThirdPartyPlatform platform;
    std::string redirectUrl;

    std::recursive_mutex mutex;
    std::vector<std::shared_ptr<ThirdPartyLoginValue>> loginValues;

    drogon::HttpClientPtr httpClient;

    std::tuple<std::string, std::string, std::string> checkAndGetPlatformConfig(const ThirdPartyPlatform& platform, const Json::Value& config);
};

// QQ第三方登录平台实现
class ThirdPartyLoginPlatform_QQ : public ThirdPartyLoginPlatformBase {
public:
    ThirdPartyLoginPlatform_QQ(const Json::Value& config);

    drogon::Task<bool> fetchTokens(std::shared_ptr<ThirdPartyLoginValue> value) override;
    drogon::Task<std::string> getAccessToken(std::shared_ptr<ThirdPartyLoginValue> value) override;
    drogon::Task<bool> fetchOpenId(std::shared_ptr<ThirdPartyLoginValue> value) override;
    drogon::Task<std::string> getOpenId(std::shared_ptr<ThirdPartyLoginValue> value) override;
    drogon::Task<std::string> getAuthorizationUrl(std::shared_ptr<ThirdPartyLoginValue> value) override;
    drogon::Task<bool> fetchThirdPartyUserInfo(std::shared_ptr<ThirdPartyLoginValue> value) override;
    drogon::Task<std::string> getThirdPartyUserNickName(std::shared_ptr<ThirdPartyLoginValue> value) override;
    drogon::Task<bool> callBack(const std::string& code, const std::string& state) override;
    drogon::Task<std::string> saveInfoToDB(std::shared_ptr<ThirdPartyLoginValue> value) override;
};

// 微信第三方登录平台实现
class ThirdPartyLoginPlatform_WeChat : public ThirdPartyLoginPlatformBase {
public:
    ThirdPartyLoginPlatform_WeChat(const Json::Value& config);

    drogon::Task<bool> fetchTokens(std::shared_ptr<ThirdPartyLoginValue> value) override;
    drogon::Task<std::string> getAccessToken(std::shared_ptr<ThirdPartyLoginValue> value) override;
    drogon::Task<bool> fetchOpenId(std::shared_ptr<ThirdPartyLoginValue> value) override;
    drogon::Task<std::string> getOpenId(std::shared_ptr<ThirdPartyLoginValue> value) override;
    drogon::Task<std::string> getAuthorizationUrl(std::shared_ptr<ThirdPartyLoginValue> value) override;
    drogon::Task<bool> fetchThirdPartyUserInfo(std::shared_ptr<ThirdPartyLoginValue> value) override;
    drogon::Task<std::string> getThirdPartyUserNickName(std::shared_ptr<ThirdPartyLoginValue> value) override;
    drogon::Task<bool> callBack(const std::string& code, const std::string& state) override;
    drogon::Task<std::string> saveInfoToDB(std::shared_ptr<ThirdPartyLoginValue> value) override;
};

// 第三方登录管理器
class ThirdPartyLoginService : public SingletonWithInit<ThirdPartyLoginService> {
public:
    ThirdPartyLoginService(const Json::Value& config);

    drogon::Task<IThirdPartyLoginPlatform*> getPlatform(ThirdPartyPlatform platform);
    drogon::Task<void> deletePlatform(ThirdPartyPlatform platform);
    drogon::Task<void> clearExpired();

private:
    std::mutex mutex;
    std::unordered_map<ThirdPartyPlatform, std::unique_ptr<IThirdPartyLoginPlatform>> platforms;
};

} // namespace Services
} // namespace UEAdminAPI

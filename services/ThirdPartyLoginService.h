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

namespace UEAdminAPI {
namespace Services {

// 第三方平台枚举
enum class ThirdPartyPlatform {
    QQ,
    WeChat,
    None
};

// 第三方登录值
class ThirdPartyLoginValue {
public:
    ThirdPartyLoginValue(const std::string& code, const std::string& verifyCode, 
                        const std::chrono::system_clock::time_point& expireTime);

    ThirdPartyLoginValue(const std::string& code, const std::string& verifyCode);

    std::string code;
    std::string authorizationCode;
    std::string verifyCode;
    std::string accessToken;
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

    virtual ThirdPartyLoginValue* getLoginValue(const std::string& code) = 0;
    virtual bool verifyCode(const std::string& code, const std::string& verifyCode) = 0;
    virtual std::shared_ptr<ThirdPartyLoginValue> createNewThirdLoginValue() = 0;
    virtual void clearExpired() = 0;
    virtual bool fetchTokens(std::shared_ptr<ThirdPartyLoginValue> value) = 0;
    virtual std::string getAccessToken(std::shared_ptr<ThirdPartyLoginValue> value) = 0;
    virtual bool fetchOpenId(std::shared_ptr<ThirdPartyLoginValue> value) = 0;
    virtual std::string getOpenId(std::shared_ptr<ThirdPartyLoginValue> value) = 0;
    virtual std::string getAuthorizationUrl(std::shared_ptr<ThirdPartyLoginValue> value) = 0;
    virtual bool fetchThirdPartyUserInfo(std::shared_ptr<ThirdPartyLoginValue> value) = 0;
    virtual std::string getThirdPartyUserNickName(std::shared_ptr<ThirdPartyLoginValue> value) = 0;
    virtual bool callBack(const std::string& code, const std::string& state) = 0;

    virtual std::string getName() const = 0;
    virtual std::string getRedirectUrl() const = 0;
};

// 第三方登录平台基类
class ThirdPartyLoginPlatformBase : public IThirdPartyLoginPlatform {
public:
    ThirdPartyLoginPlatformBase(const Json::Value& config, const std::string& name);
    virtual ~ThirdPartyLoginPlatformBase() = default;

    ThirdPartyLoginValue* getLoginValue(const std::string& code) override;
    bool verifyCode(const std::string& code, const std::string& verifyCode) override;
    std::shared_ptr<ThirdPartyLoginValue> createNewThirdLoginValue() override;
    void clearExpired() override;

    std::string getName() const override {
        return name;
    }

    std::string getRedirectUrl() const override {
        return redirectUrl;
    }

protected:
    std::string serverHost;
    std::string clientId;
    std::string clientSecret;
    std::string name;
    std::string redirectUrl;

    std::recursive_mutex mutex;
    std::vector<std::shared_ptr<ThirdPartyLoginValue>> loginValues;

    drogon::HttpClientPtr httpClient;

    std::tuple<std::string, std::string, std::string> checkAndGetPlatformConfig(const std::string& platformName, const Json::Value& config);
};

// QQ第三方登录平台实现
class ThirdPartyLoginPlatform_QQ : public ThirdPartyLoginPlatformBase {
public:
    ThirdPartyLoginPlatform_QQ(const Json::Value& config);

    bool fetchTokens(std::shared_ptr<ThirdPartyLoginValue> value) override;
    std::string getAccessToken(std::shared_ptr<ThirdPartyLoginValue> value) override;
    bool fetchOpenId(std::shared_ptr<ThirdPartyLoginValue> value) override;
    std::string getOpenId(std::shared_ptr<ThirdPartyLoginValue> value) override;
    std::string getAuthorizationUrl(std::shared_ptr<ThirdPartyLoginValue> value) override;
    bool fetchThirdPartyUserInfo(std::shared_ptr<ThirdPartyLoginValue> value) override;
    std::string getThirdPartyUserNickName(std::shared_ptr<ThirdPartyLoginValue> value) override;
    bool callBack(const std::string& code, const std::string& state) override;
};

// 微信第三方登录平台实现
class ThirdPartyLoginPlatform_WeChat : public ThirdPartyLoginPlatformBase {
public:
    ThirdPartyLoginPlatform_WeChat(const Json::Value& config);

    bool fetchTokens(std::shared_ptr<ThirdPartyLoginValue> value) override;
    std::string getAccessToken(std::shared_ptr<ThirdPartyLoginValue> value) override;
    bool fetchOpenId(std::shared_ptr<ThirdPartyLoginValue> value) override;
    std::string getOpenId(std::shared_ptr<ThirdPartyLoginValue> value) override;
    std::string getAuthorizationUrl(std::shared_ptr<ThirdPartyLoginValue> value) override;
    bool fetchThirdPartyUserInfo(std::shared_ptr<ThirdPartyLoginValue> value) override;
    std::string getThirdPartyUserNickName(std::shared_ptr<ThirdPartyLoginValue> value) override;
    bool callBack(const std::string& code, const std::string& state) override;
};

// 第三方登录管理器
class ThirdPartyLoginService : public SingletonWithInit<ThirdPartyLoginService> {
public:
    ThirdPartyLoginService(const Json::Value& config);

    IThirdPartyLoginPlatform* getPlatform(ThirdPartyPlatform platform);
    void deletePlatform(ThirdPartyPlatform platform);
    void clearExpired();

private:
    std::mutex mutex;
    std::unordered_map<ThirdPartyPlatform, std::unique_ptr<IThirdPartyLoginPlatform>> platforms;
};

} // namespace Services
} // namespace UEAdminAPI

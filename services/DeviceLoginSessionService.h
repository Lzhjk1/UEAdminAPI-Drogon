#pragma once

#include <drogon/drogon.h>
#include <drogon/CacheMap.h>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include "utils/SingletonWithInit.h"

namespace UEAdminAPI {
namespace Services {

/// @brief 设备登录会话（RFC 8628 device authorization grant）
struct DeviceLoginSessionInfo {
    std::string userCode;      ///< 用户在浏览器输入的短码（不是取 token 的凭证）
    std::string redirectUri;   ///< 完成后的 loopback 通知地址（可为空）
    int userId = -1;           ///< 完成授权后的用户 ID；未授权为 -1
    int intervalSec = 5;       ///< 客户端轮询间隔（slow_down 会递增）
    std::chrono::steady_clock::time_point lastPoll{};
    std::chrono::system_clock::time_point expiresAt;
};

/// @brief device_authorization 端点的返回值
struct DeviceAuthorization {
    std::string deviceCode;    ///< 仅返回给发起请求的客户端，轮询 token 时必须提交
    std::string userCode;      ///< 给用户在浏览器输入的短码
    int expiresIn = 600;
    int interval = 5;
};

/// @brief RFC 8628 设备会话：device_code 是秘密，user_code 只用于浏览器确认
class DeviceLoginSessionService : public SingletonWithInit<DeviceLoginSessionService> {
public:
    explicit DeviceLoginSessionService(const Json::Value &config);

    /// @brief 创建会话。deviceCode 只返回一次，服务端按它轮询；userCode 给浏览器。
    DeviceAuthorization CreateSession(const std::string &redirectUri);

    /// @brief 按用户短码查询（登录页用），不消费
    std::optional<DeviceLoginSessionInfo> FindByUserCode(const std::string &userCode) const;

    /// @brief 浏览器完成认证后标记会话已授权
    bool MarkLoggedInByUserCode(const std::string &userCode, int userId);

    /// @brief 客户端轮询 token：pending / slow_down / authorized（authorized 时一次性消费）
    enum class PollStatus {
        NotFound,
        Pending,
        SlowDown,
        Authorized
    };
    PollStatus PollAndConsume(const std::string &deviceCode, DeviceLoginSessionInfo &out);

    int GetExpireSeconds() const { return _expireSeconds; }
    int GetIntervalSeconds() const { return _intervalSeconds; }

    /// @brief 去空格/连字符、转大写，兼容用户输入 "abcd-efgh" / "ABCD EFGH"
    static std::string NormalizeUserCode(const std::string &userCode);

private:
    bool IsExpired(const DeviceLoginSessionInfo &info) const;
    void SaveSessionLocked(const std::string &deviceCode, const DeviceLoginSessionInfo &info);
    void EraseSessionLocked(const std::string &deviceCode, const std::string &userCode);

    int _expireSeconds = 600;
    int _intervalSeconds = 5;
    mutable std::mutex _mutex;
    std::unique_ptr<drogon::CacheMap<std::string, DeviceLoginSessionInfo>> _sessionCache;
    std::unique_ptr<drogon::CacheMap<std::string, std::string>> _userCodeIndex;
};

} // namespace Services
} // namespace UEAdminAPI

#pragma once

#include <drogon/drogon.h>
#include <drogon/CacheMap.h>
#include <string>
#include <memory>
#include <optional>
#include "utils/SingletonWithInit.h"

namespace UEAdminAPI {
namespace Services {

/// @brief 设备登录会话信息（OAuth2 device-like flow）
struct DeviceLoginSessionInfo {
    std::string state;        ///< 一次性随机 state
    std::string redirectUri;  ///< 登录完成后的 loopback 通知地址（可为空，空则保留 ueloginreturn 兼容路径）
    int userId = -1;          ///< 登录完成后写入用户ID；未登录为 -1
};

/// @brief 设备登录会话服务：创建 / 查询 / 标记完成 / 一次性消费
/// 使用 Drogon CacheMap 自动处理过期与线程安全。
class DeviceLoginSessionService : public SingletonWithInit<DeviceLoginSessionService> {
public:
    explicit DeviceLoginSessionService(const Json::Value &config);

    /// @brief 创建一次性设备登录会话
    /// @param redirectUri loopback 通知地址（可为空）
    /// @return 生成的 state
    std::string CreateSession(const std::string &redirectUri);

    /// @brief 查询会话（不消费）
    /// @return 会话存在且未过期返回信息，否则 std::nullopt
    std::optional<DeviceLoginSessionInfo> FindSession(const std::string &state) const;

    /// @brief 标记会话登录完成
    /// @return 会话存在且未消费返回 true；否则 false
    bool MarkLoggedIn(const std::string &state, int userId);

    /// @brief 消费已登录会话（取后即废）
    /// @return 成功返回已登录会话信息并删除；不存在/未登录/已消费返回 std::nullopt
    std::optional<DeviceLoginSessionInfo> ConsumeSession(const std::string &state);

    /// @brief 会话有效期（秒）
    int GetExpireSeconds() const { return _expireSeconds; }

private:
    int _expireSeconds = 600; ///< 默认 10 分钟
    std::unique_ptr<drogon::CacheMap<std::string, DeviceLoginSessionInfo>> _sessionCache;
};

} // namespace Services
} // namespace UEAdminAPI

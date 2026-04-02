#pragma once

#include <drogon/drogon.h>
#include <drogon/CacheMap.h>
#include <string>
#include <memory>
#include <unordered_map>
#include "utils/SingletonWithInit.h"
#include "utils/MFA/eMFA_Type.h"
#include "utils/HttpResult.h"

namespace UEAdminAPI {
namespace Services {

struct ActionTokenInfo {
    int userId;
    eMFAType mfaType;
    std::string boundTarget;
};

class ActionTokenService : public SingletonWithInit<ActionTokenService> {
public:
    ActionTokenService(const Json::Value& config);
    ~ActionTokenService() = default;

    /// @brief 生成一个指定业务类别的 ActionToken
    /// @param userId 用户ID
    /// @param mfaType 业务类别 (复用 eMFAType)
    /// @return 生成的不透明随机 Token 字符串
    std::string GenerateToken(int userId, eMFAType mfaType, const std::string& boundTarget = "");

    /// @brief 验证并消耗 ActionToken
    /// @param token 客户端提供的 Token 字符串
    /// @param expectedAction 期望的业务类别
    /// @param userId 当前登录用户的ID (用于双重校验)
    /// @return 验证通过返回 true，否则返回 false (Token 不存在、过期、类型不符或所属用户不符都会返回 false)
    bool VerifyAndConsumeToken(const std::string& token, eMFAType expectedAction, int userId, const std::string& requestTarget = "");

    /// @brief 获取路由对应的 Action 类别
    /// @param path 路由路径 (例如: "/user/delete")
    /// @param method HTTP 请求方法
    /// @return 对应的 Action 类别 (eMFAType)，如果不存在则返回 eMFAType::Error
    eMFAType GetActionByRoute(const std::string& path, drogon::HttpMethod method) const;

    /// @brief 获取 ActionToken 过期时间
    int GetExpireSeconds() const { return _expireSeconds; }

    /// @brief 为用户生成 ActionToken 的核心逻辑 (包含 MFA 校验等业务逻辑)
    /// @param userId 用户ID, 登录用户传入大于0的值，匿名用户传入 -1
    drogon::Task<UEAdminAPI::utils::HttpResult> GenerateTokenCore(
        int userId,
        const std::string& mfaTypeStr,
        const std::string& mfaCode,
        const std::string& target);

    /// @brief 为登录用户生成 ActionToken (包含 MFA 校验等业务逻辑)
    drogon::Task<UEAdminAPI::utils::HttpResult> GenerateTokenForUser(
        int userId,
        const std::string& mfaTypeStr,
        const std::string& mfaCode,
        const std::string& target);

    /// @brief 为未登录用户生成 ActionToken (包含 MFA 校验等业务逻辑)
    drogon::Task<UEAdminAPI::utils::HttpResult> GenerateAnonymousToken(
        const std::string& mfaTypeStr,
        const std::string& mfaCode,
        const std::string& target);

private:
    // 使用 Drogon 内置的线程安全缓存，自动处理过期
    std::unique_ptr<drogon::CacheMap<std::string, ActionTokenInfo>> _tokenCache;

    // 统一映射表配置：路由 -> Action 类别 (eMFAType)
    std::unordered_map<std::string, eMFAType> _routeToActionMap;

    // ActionToken 过期时间
    int _expireSeconds = 300;
};

} // namespace Services
} // namespace UEAdminAPI

#include "ActionTokenService.h"
#include <drogon/utils/Utilities.h>

namespace UEAdminAPI {
namespace Services {

ActionTokenService::ActionTokenService(const Json::Value& config) {
    if (!config.isMember("UserManage")) {
        LOG_FATAL << "配置中缺少 UserManage 节点";
        return;
    }
    if (config["UserManage"].isMember("ActionTokenSec") && config["UserManage"]["ActionTokenSec"].isInt()) {
        _expireSeconds = config["UserManage"]["ActionTokenSec"].asInt();
    } else {
        _expireSeconds = 300; // 默认5分钟
    }

    // 初始化 CacheMap，主循环通过 drogon 自身的事件循环来清理过期项
    _tokenCache = std::make_unique<drogon::CacheMap<std::string, ActionTokenInfo>>(
        drogon::app().getLoop(), 
        1.0 // 清理步长(秒)
    );

    // ========================================
    // 路由 -> Action 类别 (复用 eMFAType)
    // ========================================
    _routeToActionMap["/user/delete"] = eMFAType::DeleteUser;
    _routeToActionMap["/api/third/unbind"] = eMFAType::Unbind;
    _routeToActionMap["/user/update"] = eMFAType::ModifyUser;
    _routeToActionMap["/api/third/bind"] = eMFAType::ThirdPartyBind;
    _routeToActionMap["/user/login/email"] = eMFAType::Login;
    _routeToActionMap["/user/login/phone"] = eMFAType::Login;
    _routeToActionMap["/user/create"] = eMFAType::Register;
    _routeToActionMap["/user/create/phone"] = eMFAType::Register;
    _routeToActionMap["/api/third/register"] = eMFAType::Register;

    LOG_INFO << "ActionTokenService 初始化完成";
}

eMFAType ActionTokenService::GetActionByRoute(const std::string& path) const {
    auto it = _routeToActionMap.find(path);
    if (it != _routeToActionMap.end()) {
        return it->second;
    }
    return eMFAType::Error;
}

std::string ActionTokenService::GenerateToken(int userId, eMFAType mfaType) {
    // 使用 Drogon 的 getUuid 生成一个 UUID，去掉连字符作为不透明 Token
    std::string token = drogon::utils::getUuid();
    token.erase(std::remove(token.begin(), token.end(), '-'), token.end());

    ActionTokenInfo info;
    info.userId = userId;
    info.mfaType = mfaType;

    // 存入缓存并设置过期时间
    _tokenCache->insert(token, info, _expireSeconds);

    return token;
}

bool ActionTokenService::VerifyAndConsumeToken(const std::string& token, eMFAType expectedAction, int userId) {
    if (token.empty()) {
        return false;
    }

    ActionTokenInfo info;
    bool found = _tokenCache->findAndFetch(token, info);
    if (!found) {
        return false;
    }

    // 校验归属和业务类别
    if (info.userId == userId && info.mfaType == expectedAction) {
        // 校验成功，消耗掉这个 Token（保证单次使用）
        _tokenCache->erase(token);
        return true;
    }

    return false;
}

} // namespace Services
} // namespace UEAdminAPI
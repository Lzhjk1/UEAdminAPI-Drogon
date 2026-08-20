#include "DeviceLoginSessionService.h"
#include <drogon/utils/Utilities.h>
#include <algorithm>
#include <trantor/utils/Logger.h>

namespace UEAdminAPI {
namespace Services {

DeviceLoginSessionService::DeviceLoginSessionService(const Json::Value &config) {
    if (config.isMember("UserManage") &&
        config["UserManage"].isMember("DeviceLoginSec") &&
        config["UserManage"]["DeviceLoginSec"].isInt()) {
        _expireSeconds = config["UserManage"]["DeviceLoginSec"].asInt();
        if (_expireSeconds <= 0) {
            _expireSeconds = 600;
        }
    }

    // Drogon CacheMap：主事件循环自动清理过期项，线程安全
    _sessionCache = std::make_unique<drogon::CacheMap<std::string, DeviceLoginSessionInfo>>(
        drogon::app().getLoop(),
        1.0 // 清理步长（秒）
    );

    LOG_INFO << "DeviceLoginSessionService 初始化完成, TTL=" << _expireSeconds << "s";
}

std::string DeviceLoginSessionService::CreateSession(const std::string &redirectUri) {
    std::string state = drogon::utils::getUuid();
    state.erase(std::remove(state.begin(), state.end(), '-'), state.end());

    DeviceLoginSessionInfo info;
    info.state = state;
    info.redirectUri = redirectUri;
    info.userId = -1;

    _sessionCache->insert(state, info, _expireSeconds);
    return state;
}

std::optional<DeviceLoginSessionInfo> DeviceLoginSessionService::FindSession(const std::string &state) const {
    if (state.empty()) {
        return std::nullopt;
    }
    DeviceLoginSessionInfo info;
    if (_sessionCache->findAndFetch(state, info)) {
        return info;
    }
    return std::nullopt;
}

bool DeviceLoginSessionService::MarkLoggedIn(const std::string &state, int userId) {
    if (state.empty() || userId <= 0) {
        return false;
    }

    DeviceLoginSessionInfo info;
    if (!_sessionCache->findAndFetch(state, info)) {
        return false;
    }
    if (info.userId > 0) {
        return false; // 已登录完成，不允许重复绑定
    }

    info.userId = userId;
    // CacheMap::insert 不覆盖已存在 key，必须先 erase 再 insert
    _sessionCache->erase(state);
    _sessionCache->insert(state, info, _expireSeconds);
    return true;
}

std::optional<DeviceLoginSessionInfo> DeviceLoginSessionService::ConsumeSession(const std::string &state) {
    if (state.empty()) {
        return std::nullopt;
    }

    DeviceLoginSessionInfo info;
    if (!_sessionCache->findAndFetch(state, info)) {
        return std::nullopt;
    }
    if (info.userId <= 0) {
        return std::nullopt;
    }

    // 只有已登录会话才允许消费；消费即删除，避免并发 check 重复领取。
    _sessionCache->erase(state);
    return info;
}

} // namespace Services
} // namespace UEAdminAPI

#include "DeviceLoginSessionService.h"

#include <drogon/utils/Utilities.h>
#include <openssl/rand.h>
#include <algorithm>
#include <cctype>
#include <random>
#include <trantor/utils/Logger.h>

namespace UEAdminAPI {
namespace Services {

namespace {

constexpr const char *kUserCodeAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

std::string GenerateDeviceCode() {
    std::string code = drogon::utils::getUuid();
    code.erase(std::remove(code.begin(), code.end(), '-'), code.end());
    return code;
}

std::string GenerateUserCode() {
    unsigned char bytes[8] = {0};
    if (RAND_bytes(bytes, sizeof(bytes)) != 1) {
        for (auto &b : bytes) {
            b = static_cast<unsigned char>(std::random_device{}());
        }
    }
    std::string code;
    code.reserve(8);
    for (unsigned char b : bytes) {
        code.push_back(kUserCodeAlphabet[b % (sizeof(kUserCodeAlphabet) - 1)]);
    }
    return code;
}

std::string FormatUserCode(const std::string &raw) {
    if (raw.size() != 8) {
        return raw;
    }
    return raw.substr(0, 4) + "-" + raw.substr(4);
}

} // namespace

DeviceLoginSessionService::DeviceLoginSessionService(const Json::Value &config) {
    if (config.isMember("UserManage")) {
        const auto &userManage = config["UserManage"];
        if (userManage.isMember("DeviceLoginSec") && userManage["DeviceLoginSec"].isInt()) {
            _expireSeconds = userManage["DeviceLoginSec"].asInt();
            if (_expireSeconds <= 0) {
                _expireSeconds = 600;
            }
        }
        if (userManage.isMember("DeviceLoginIntervalSec") &&
            userManage["DeviceLoginIntervalSec"].isInt()) {
            _intervalSeconds = userManage["DeviceLoginIntervalSec"].asInt();
            if (_intervalSeconds <= 0) {
                _intervalSeconds = 5;
            }
        }
    }

    _sessionCache = std::make_unique<drogon::CacheMap<std::string, DeviceLoginSessionInfo>>(
        drogon::app().getLoop(), 1.0);
    _userCodeIndex = std::make_unique<drogon::CacheMap<std::string, std::string>>(
        drogon::app().getLoop(), 1.0);

    LOG_INFO << "DeviceLoginSessionService 初始化完成, TTL=" << _expireSeconds
             << "s, interval=" << _intervalSeconds << "s";
}

std::string DeviceLoginSessionService::NormalizeUserCode(const std::string &userCode) {
    std::string normalized;
    normalized.reserve(userCode.size());
    for (unsigned char ch : userCode) {
        if (std::isalnum(ch)) {
            normalized.push_back(static_cast<char>(std::toupper(ch)));
        }
    }
    if (normalized.size() > 16) {
        normalized.resize(16);
    }
    return normalized;
}

bool DeviceLoginSessionService::IsExpired(const DeviceLoginSessionInfo &info) const {
    return info.expiresAt <= std::chrono::system_clock::now();
}

void DeviceLoginSessionService::SaveSessionLocked(const std::string &deviceCode,
                                                  const DeviceLoginSessionInfo &info) {
    _sessionCache->erase(deviceCode);
    _sessionCache->insert(deviceCode, info, _expireSeconds);
}

void DeviceLoginSessionService::EraseSessionLocked(const std::string &deviceCode,
                                                   const std::string &userCode) {
    _sessionCache->erase(deviceCode);
    if (!userCode.empty()) {
        _userCodeIndex->erase(userCode);
    }
}

DeviceAuthorization DeviceLoginSessionService::CreateSession(const std::string &redirectUri) {
    std::lock_guard<std::mutex> lock(_mutex);

    DeviceAuthorization authorization;
    authorization.deviceCode = GenerateDeviceCode();
    authorization.expiresIn = _expireSeconds;
    authorization.interval = _intervalSeconds;

    // user_code 不是秘密，但必须唯一，避免两个设备覆盖同一个浏览器会话。
    std::string normalized;
    for (int attempt = 0; attempt < 20; ++attempt) {
        normalized = GenerateUserCode();
        std::string existing;
        if (!_userCodeIndex->findAndFetch(normalized, existing)) {
            break;
        }
        normalized.clear();
    }
    if (normalized.empty()) {
        LOG_ERROR << "DeviceLoginSessionService: 无法生成唯一 user_code";
        return authorization;
    }

    DeviceLoginSessionInfo info;
    info.userCode = normalized;
    info.redirectUri = redirectUri;
    info.userId = -1;
    info.intervalSec = _intervalSeconds;
    info.expiresAt = std::chrono::system_clock::now() + std::chrono::seconds(_expireSeconds);

    SaveSessionLocked(authorization.deviceCode, info);
    _userCodeIndex->insert(normalized, authorization.deviceCode, _expireSeconds);

    authorization.userCode = FormatUserCode(normalized);
    return authorization;
}

std::optional<DeviceLoginSessionInfo> DeviceLoginSessionService::FindByUserCode(
    const std::string &userCode) const {
    const std::string normalized = NormalizeUserCode(userCode);
    if (normalized.empty()) {
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(_mutex);
    std::string deviceCode;
    if (!_userCodeIndex->findAndFetch(normalized, deviceCode)) {
        return std::nullopt;
    }

    DeviceLoginSessionInfo info;
    if (!_sessionCache->findAndFetch(deviceCode, info) || IsExpired(info)) {
        _sessionCache->erase(deviceCode);
        _userCodeIndex->erase(normalized);
        return std::nullopt;
    }
    return info;
}

bool DeviceLoginSessionService::MarkLoggedInByUserCode(const std::string &userCode, int userId) {
    if (userId <= 0) {
        return false;
    }
    const std::string normalized = NormalizeUserCode(userCode);
    if (normalized.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(_mutex);
    std::string deviceCode;
    if (!_userCodeIndex->findAndFetch(normalized, deviceCode)) {
        return false;
    }

    DeviceLoginSessionInfo info;
    if (!_sessionCache->findAndFetch(deviceCode, info) || IsExpired(info)) {
        EraseSessionLocked(deviceCode, normalized);
        return false;
    }
    if (info.userId > 0) {
        return false;
    }

    info.userId = userId;
    // 清零轮询时间戳, 放行紧接的客户端请求, 避免触发 slow_down
    info.lastPoll = std::chrono::steady_clock::time_point{};
    SaveSessionLocked(deviceCode, info);
    return true;
}

DeviceLoginSessionService::PollStatus DeviceLoginSessionService::PollAndConsume(
    const std::string &deviceCode, DeviceLoginSessionInfo &out) {
    if (deviceCode.empty()) {
        return PollStatus::NotFound;
    }

    std::lock_guard<std::mutex> lock(_mutex);
    DeviceLoginSessionInfo info;
    if (!_sessionCache->findAndFetch(deviceCode, info) || IsExpired(info)) {
        EraseSessionLocked(deviceCode, info.userCode);
        return PollStatus::NotFound;
    }

    const auto now = std::chrono::steady_clock::now();
    if (info.lastPoll != std::chrono::steady_clock::time_point{} &&
        now - info.lastPoll < std::chrono::seconds(info.intervalSec)) {
        // RFC 8628: slow_down 后客户端应把间隔增加 5 秒。
        // (std::min) 的括号避开 windows.h 的 min 宏
        info.intervalSec = (std::min)(info.intervalSec + 5, 60);
        SaveSessionLocked(deviceCode, info);
        out = info;
        return PollStatus::SlowDown;
    }
    info.lastPoll = now;
    SaveSessionLocked(deviceCode, info);

    out = info;
    if (info.userId > 0) {
        EraseSessionLocked(deviceCode, info.userCode);
        return PollStatus::Authorized;
    }
    return PollStatus::Pending;
}

} // namespace Services
} // namespace UEAdminAPI

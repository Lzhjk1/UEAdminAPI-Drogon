#include "ActionTokenService.h"
#include <drogon/utils/Utilities.h>
#include "services/AuthService.h"
#include "services/MFAService.h"
#include "models/User.h"
#include "utils/ApiErrorCodes.h"
#include <algorithm>
#include <cctype>

using namespace UEAdminAPI::utils;

namespace UEAdminAPI {
namespace Services {

namespace {
std::string NormalizeTarget(const std::string& rawTarget) {
    std::string normalized = rawTarget;
    normalized.erase(
        normalized.begin(),
        std::find_if(normalized.begin(), normalized.end(), [](unsigned char ch) { return !std::isspace(ch); })
    );
    normalized.erase(
        std::find_if(normalized.rbegin(), normalized.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(),
        normalized.end()
    );

    if (normalized.find('@') != std::string::npos) {
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
    }

    return normalized;
}
}

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
    // 路由+Method -> Action 类别 (复用 eMFAType)
    // 格式: "METHOD:PATH"
    // ========================================
    _routeToActionMap["DELETE:/user/delete"] = eMFAType::DeleteUser;
    _routeToActionMap["POST:/api/third/unbind"] = eMFAType::Unbind;
    _routeToActionMap["POST:/user/update"] = eMFAType::ModifyUser;
    _routeToActionMap["POST:/api/third/bind"] = eMFAType::ThirdPartyBind;
    _routeToActionMap["GET:/user/login/email"] = eMFAType::Login;
    _routeToActionMap["GET:/user/login/phone"] = eMFAType::Login;
    _routeToActionMap["POST:/user/create"] = eMFAType::Register;
    _routeToActionMap["POST:/user/create/phone"] = eMFAType::Register;
    _routeToActionMap["POST:/api/third/register"] = eMFAType::Register;

    LOG_INFO << "ActionTokenService 初始化完成";
}

eMFAType ActionTokenService::GetActionByRoute(const std::string& path, drogon::HttpMethod method) const {
    std::string methodStr;
    switch (method) {
        case drogon::Get: methodStr = "GET"; break;
        case drogon::Post: methodStr = "POST"; break;
        case drogon::Put: methodStr = "PUT"; break;
        case drogon::Delete: methodStr = "DELETE"; break;
        case drogon::Patch: methodStr = "PATCH"; break;
        case drogon::Options: methodStr = "OPTIONS"; break;
        case drogon::Head: methodStr = "HEAD"; break;
        default: methodStr = "UNKNOWN"; break;
    }
    
    std::string key = methodStr + ":" + path;
    auto it = _routeToActionMap.find(key);
    if (it != _routeToActionMap.end()) {
        return it->second;
    }
    return eMFAType::Error;
}

std::string ActionTokenService::GenerateToken(int userId, eMFAType mfaType, const std::string& boundTarget) {
    // 使用 Drogon 的 getUuid 生成一个 UUID，去掉连字符作为不透明 Token
    std::string token = drogon::utils::getUuid();
    token.erase(std::remove(token.begin(), token.end(), '-'), token.end());

    ActionTokenInfo info;
    info.userId = userId;
    info.mfaType = mfaType;
    info.boundTarget = NormalizeTarget(boundTarget);

    // 存入缓存并设置过期时间
    _tokenCache->insert(token, info, _expireSeconds);

    return token;
}

bool ActionTokenService::VerifyAndConsumeToken(const std::string& token, eMFAType expectedAction, int userId, const std::string& requestTarget) {
    if (token.empty()) {
        return false;
    }

    ActionTokenInfo info;
    bool found = _tokenCache->findAndFetch(token, info);
    if (!found) {
        return false;
    }

    bool needTargetMatch = (info.userId <= 0 && !info.boundTarget.empty());
    if (needTargetMatch) {
        std::string normalizedRequestTarget = NormalizeTarget(requestTarget);
        if (normalizedRequestTarget.empty() || normalizedRequestTarget != info.boundTarget) {
            return false;
        }
    }

    if (info.userId == userId && info.mfaType == expectedAction) {
        // 校验成功，消耗掉这个 Token（保证单次使用）
        _tokenCache->erase(token);
        return true;
    }

    return false;
}

drogon::Task<UEAdminAPI::utils::HttpResult> ActionTokenService::GenerateTokenCore(int userId, const std::string& mfaTypeStr, const std::string& mfaCode, const std::string& target) {
    UEAdminAPI::utils::HttpResult result;
    auto _authService = AuthService::Instance();
    auto _mfaService = MFAService::Instance();

    if (mfaTypeStr.empty()) {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "缺少参数: mfaType");
        co_return result;
    }

    eMFAType mfaType = stringToMFAType(mfaTypeStr);
    if (mfaType == eMFAType::Error) {
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "未知的操作类别 (mfaType)");
        co_return result;
    }

    bool isAnonymous = (userId <= 0);

    if (isAnonymous) {
        // 匿名申请操作类型检查
        if (mfaType != eMFAType::Login && mfaType != eMFAType::Register && mfaType != eMFAType::LoginOrRegister && mfaType != eMFAType::ResetPassword) {
            result.setResult(ApiErrorCode::ApiError_InvalidOperation, "此操作类型不允许匿名申请");
            co_return result;
        }

        // 未登录状态，只需验证验证码是否正确，无需校验归属
        if (mfaCode.empty() || target.empty()) {
            result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "需要提供 mfaCode 和 target");
            co_return result;
        }

        auto [isSuccess, errMsg] = co_await _mfaService->VerifyTheCode(target, mfaCode, mfaType);
        if (!isSuccess) {
            result.setResult(ApiErrorCode::ApiError_InvalidVerifyCode, errMsg.empty() ? std::string("验证码错误") : errMsg);
            co_return result;
        }
        
        // 确保匿名情况下的userId为-1
        userId = -1; 
    } else {
        auto dbClientPtr = drogon::app().getDbClient();
        drogon::orm::Mapper<drogon_model::UEAdminAPI::User> mapperUser(dbClientPtr);
        drogon_model::UEAdminAPI::User user;

        try {
            user = mapperUser.findByPrimaryKey(userId);
        } catch (...) {
            result.setResult(ApiErrorCode::ApiError_UserNotFound);
            co_return result;
        }

        bool hasEmail = user.getEmail() && !user.getValueOfEmail().empty();
        bool hasPhone = user.getTelephoneNumber() && !user.getValueOfTelephoneNumber().empty();

        if (hasEmail || hasPhone) {
            if (mfaCode.empty() || target.empty()) {
                result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "需要提供 mfaCode 和 target");
                co_return result;
            }

            auto [mfaOk, mfaErr] = co_await _authService->VerifyUserTargetMFA(target, mfaCode, user, mfaType);
            if (!mfaOk) {
                result.setResult(ApiErrorCode::ApiError_InvalidVerifyCode, mfaErr.empty() ? std::string("验证码错误") : mfaErr);
                co_return result;
            }
        }
    }

    std::string tokenTarget = isAnonymous ? target : "";
    std::string token = GenerateToken(userId, mfaType, tokenTarget);
    result.setResult(ApiErrorCode::ApiError_Success, "ActionToken 颁发成功");
    result.jsondata["actionToken"] = token;
    result.jsondata["mfaType"] = mfaTypeStr;
    result.jsondata["expiresIn"] = GetExpireSeconds();

    co_return result;
}

drogon::Task<UEAdminAPI::utils::HttpResult> ActionTokenService::GenerateTokenForUser(int userId, const std::string& mfaTypeStr, const std::string& mfaCode, const std::string& target) {
    if (userId <= 0) {
        UEAdminAPI::utils::HttpResult result;
        result.setResult(ApiErrorCode::ApiError_AuthenticationFailed, "未登录");
        co_return result;
    }
    co_return co_await GenerateTokenCore(userId, mfaTypeStr, mfaCode, target);
}

drogon::Task<UEAdminAPI::utils::HttpResult> ActionTokenService::GenerateAnonymousToken(const std::string& mfaTypeStr, const std::string& mfaCode, const std::string& target) {
    co_return co_await GenerateTokenCore(-1, mfaTypeStr, mfaCode, target);
}

} // namespace Services
} // namespace UEAdminAPI

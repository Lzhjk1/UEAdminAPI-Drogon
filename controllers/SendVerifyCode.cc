#include "SendVerifyCode.h"
#include "utils/HttpResult.h"

#include <algorithm>
#include <cctype>

// Add definition of your processing function here

using namespace UEAdminAPI;
using namespace UEAdminAPI::utils;

namespace {
constexpr auto kTargetCooldown = std::chrono::seconds(60);
constexpr auto kTargetWindow = std::chrono::hours(1);
constexpr size_t kMaxTargetSendsPerWindow = 5;
constexpr size_t kMaxTrackedTargets = 20000;

std::string NormalizeRateLimitTarget(const std::string &target) {
    if (target.find('@') == std::string::npos) {
        return target;
    }
    std::string normalized = target;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized;
}

std::string ReadJsonOrQuery(const HttpRequestPtr &req, const char *key) {
    auto json = req->getJsonObject();
    if (json && json->isMember(key) && (*json)[key].isString()) {
        return (*json)[key].asString();
    }
    return req->getParameter(key);
}
} // namespace

bool SendVerifyCode::IsTargetRateLimited(const std::string &target) {
    if (target.empty()) {
        return false;
    }
    const std::string key = NormalizeRateLimitTarget(target);
    std::lock_guard<std::mutex> lock(_mutexForColdDownMap);
    const auto now = std::chrono::steady_clock::now();

    auto histIt = _targetSendHistory.find(key);
    if (histIt != _targetSendHistory.end()) {
        auto &history = histIt->second;
        while (!history.empty() && now - history.front() > kTargetWindow) {
            history.pop_front();
        }
        if (history.size() >= kMaxTargetSendsPerWindow) {
            return true;
        }
    }

    auto lastIt = _targetLastSent.find(key);
    if (lastIt != _targetLastSent.end() && now - lastIt->second < kTargetCooldown) {
        return true;
    }
    return false;
}

void SendVerifyCode::RecordSuccessfulSend(const std::string &target) {
    if (target.empty()) {
        return;
    }
    const std::string key = NormalizeRateLimitTarget(target);
    std::lock_guard<std::mutex> lock(_mutexForColdDownMap);
    const auto now = std::chrono::steady_clock::now();
    _targetLastSent[key] = now;

    auto &history = _targetSendHistory[key];
    while (!history.empty() && now - history.front() > kTargetWindow) {
        history.pop_front();
    }
    history.push_back(now);

    if (_targetSendHistory.size() > kMaxTrackedTargets) {
        _targetSendHistory.clear();
        _targetLastSent.clear();
    }
}

Task<HttpResponsePtr> SendVerifyCode::SendCode(HttpRequestPtr req, std::string target, std::string type) {
    // 依赖
    auto _mfaService = MFAService::Instance();

	HttpResult result;

    const bool testMode = TestModeConfig::Enable();

    // 检查IP是否处于冷却
    if(IsInColdDown(req->getPeerAddr().toIp())) {    
        result.setResult(ApiErrorCode::ApiError_SendVerifyCodeTooFrequent, "发送过于频繁, 请稍后再试");
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        co_return resp;
    }

    // 同一目标 60 秒内只能发一次、每小时最多 5 次；测试模式保留原有宽松策略。
    if (!testMode && IsTargetRateLimited(target)) {
        result.setResult(ApiErrorCode::ApiError_SendVerifyCodeTooFrequent, "该账号验证码发送过于频繁, 请稍后再试");
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        co_return resp;
    }

    auto [isSuccess, errMsg] = co_await _mfaService->SendTheCode(target, stringToMFAType(type));

    if (!isSuccess) {
        result.setResult(ApiErrorCode::ApiError_InternalError, errMsg);
    } else {
        if (!testMode) {
            RecordSuccessfulSend(target);
        }
        result.setResult(ApiErrorCode::ApiError_Success);
    }
    
    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    co_return resp;
}

Task<HttpResponsePtr> SendVerifyCode::SendCodePost(HttpRequestPtr req) {
    co_return co_await SendCode(req,
                                ReadJsonOrQuery(req, "target"),
                                ReadJsonOrQuery(req, "type"));
}

Task<HttpResponsePtr> SendVerifyCode::CheckCodePost(HttpRequestPtr req) {
    co_return co_await CheckCode(req,
                                 ReadJsonOrQuery(req, "target"),
                                 ReadJsonOrQuery(req, "mfaCode"),
                                 ReadJsonOrQuery(req, "type"));
}

Task<HttpResponsePtr> SendVerifyCode::CheckCode(HttpRequestPtr req, std::string target, std::string mfaCode, std::string type) {
    auto _mfaService = MFAService::Instance();
    HttpResult result;

    if (target.empty() || mfaCode.empty() || type.empty()) {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "缺少参数 target, mfaCode 或 type");
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        co_return resp;
    }

    auto [isSuccess, errMsg] = co_await _mfaService->VerifyTheCode(target, mfaCode, stringToMFAType(type), false);
    
    if (isSuccess) {
        result.setResult(ApiErrorCode::ApiError_Success, "验证码正确");
    } else {
        result.setResult(ApiErrorCode::ApiError_InvalidVerifyCode, errMsg.empty() ? "验证码错误" : errMsg);
    }

    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    co_return resp;
}

int SendVerifyCode::EffectiveColdDown() const {
    // 测试模式启用时, 使用 TestMode.ColdDownSec (0 表示不限制); 否则使用默认 _coldDownTime
    if (TestModeConfig::Enable())
        return TestModeConfig::ColdDownSec();
    return _coldDownTime;
}

bool SendVerifyCode::IsInColdDown(std::string ipAddr) {
    std::lock_guard<std::mutex> lock(_mutexForColdDownMap);

    int coldDown = EffectiveColdDown();
    // 冷却时间为 0 表示不限制
    if (coldDown <= 0) {
        return false;
    }

    bool isInColdDown = true;

    auto it = _coldDownMap.find(ipAddr);
    // 没找到记录, 就增加这一条记录并返回false, 即没有处于冷却中
    if(it == _coldDownMap.end()) {
        _coldDownMap[ipAddr] = std::chrono::system_clock::now();
        isInColdDown = false;
    }
    // 找到了记录, 检查是否处于冷却中
    else if(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - it->second).count() < coldDown) {
        isInColdDown =  true;
    }
    else{
        _coldDownMap[ipAddr] = std::chrono::system_clock::now();
        isInColdDown =  false;
    }

    // 清除过期的记录
    for(auto it = _coldDownMap.begin(); it != _coldDownMap.end();) {
        if(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - it->second).count() > coldDown) {
            it = _coldDownMap.erase(it);
        }
        else {
            it++;
        }
    }

    return isInColdDown;
}

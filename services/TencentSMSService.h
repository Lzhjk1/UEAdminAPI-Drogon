#pragma once
/// @file TencentSMSService.h
/// @brief 腾讯云短信服务 — 纯 HTTP 实现（TC3-HMAC-SHA256 签名）

#include <iostream>
#include <map>
#include <unordered_map>
#include <mutex>
#include <utils/MFA/eMFA_Type.h>
#include <json/json.h>
#include <drogon/utils/coroutine.h>
#include "utils/MFA/ISmsService.h"
#include "utils/SingletonWithInit.h"

using namespace std;
using namespace drogon;

/// @brief 腾讯云短信服务，通过 HTTP API 直连（不依赖 Tencent SDK）
/// 使用 TC3-HMAC-SHA256 签名算法，全平台通用
class TencentSMSService : public ISmsService, public SingletonWithInit<TencentSMSService> {
private:
    string _secretId, _secretKey, _region, _smsSdkAppId, _signName;
    unordered_map<eMFAType, std::string> _templateIds;

    /// TC3-HMAC-SHA256 签名
    string sign(const string& secretKey, const string& date,
                const string& service, const string& stringToSign) const;

public:
    /// @brief 构造函数，读取配置文件并初始化
    /// @param config Drogon 自定义配置 JSON 对象
    TencentSMSService(const Json::Value& config);

    /// @brief 发送短信
    /// @param phoneNumber 手机号（不含国家码前缀，内部自动加 +86）
    /// @param type MFA 类型，用于查找对应模板 ID
    /// @param templateParams 短信模板参数列表
    /// @return true 表示发送成功
    Task<bool> SendSms(const string& phoneNumber, eMFAType type, const vector<string>& templateParams) override;
};

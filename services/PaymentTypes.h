#pragma once

#include <cstdint>
#include <string>

namespace UEAdminAPI {
namespace Services {

struct PayCreateResult {
    bool ok = false;
    std::string qrContent;       // 微信 code_url / 支付宝 qr_code（QR 模式）
    std::string payUrl;          // 支付宝电脑网站支付收银台地址（page 模式）
    std::string providerTradeNo; // 预下单阶段可能为空
    std::string error;
};

struct PayQueryResult {
    bool ok = false;
    bool paid = false;
    bool closed = false;
    std::string state;           // 渠道原始状态（仅日志/排查用）
    std::string providerTradeNo;
    int64_t paidCents = 0;
    std::string error;
};

struct PayNotifyResult {
    bool ok = false;
    bool paid = false;
    std::string orderNo;
    std::string providerTradeNo;
    int64_t paidCents = 0;
    std::string state;
    std::string error;
};

struct PayCloseResult {
    bool ok = false;
    bool alreadyPaid = false;    // 渠道侧已支付，无法关闭
    std::string state;           // 渠道原始状态（仅日志/排查用）
    std::string error;
};

struct PayRefundResult {
    bool ok = false;          // 渠道受理成功（不代表已到账）
    bool success = false;     // 退款已完成
    bool processing = false;  // 渠道处理中，需要后续查询
    std::string providerRefundNo;
    std::string state;
    std::string error;
};

} // namespace Services
} // namespace UEAdminAPI

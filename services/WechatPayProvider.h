#pragma once

#include "PaymentTypes.h"

#include <drogon/utils/coroutine.h>

#include <map>
#include <string>

namespace UEAdminAPI {
namespace Services {

// 微信支付 Native（APIv3）：请求签名、回调验签 + AES-256-GCM 解密、按商户订单号查单。
// 回调验签使用「微信支付公钥」模式（商户平台下载 pub_key.pem + 公钥 ID）。
class WechatPayProvider {
public:
    static drogon::Task<PayCreateResult> CreateNative(const std::string &orderNo,
                                                      int64_t amountCents,
                                                      const std::string &description);

    static drogon::Task<PayQueryResult> Query(const std::string &orderNo);

    // 关闭未支付订单：渠道无单视为成功，已支付返回 alreadyPaid
    static drogon::Task<PayCloseResult> Close(const std::string &orderNo);

    // 渠道退款（APIv3）：退款申请与退款查询
    static drogon::Task<PayRefundResult> Refund(const std::string &orderNo,
                                                const std::string &refundNo,
                                                int64_t refundCents,
                                                int64_t totalCents,
                                                const std::string &reason);

    static drogon::Task<PayRefundResult> QueryRefund(const std::string &refundNo);

    // headers 使用 HTTP 头原始名称；内部按小写匹配。
    static PayNotifyResult ParseNotify(const std::map<std::string, std::string> &headers,
                                       const std::string &body);

private:
    static std::string BuildAuthorization(const std::string &method,
                                          const std::string &pathWithQuery,
                                          const std::string &body);
    static bool VerifySignature(const std::string &timestamp,
                                const std::string &nonce,
                                const std::string &body,
                                const std::string &signature,
                                const std::string &serial);
};

} // namespace Services
} // namespace UEAdminAPI

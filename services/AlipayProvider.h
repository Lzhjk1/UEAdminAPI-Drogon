#pragma once

#include "PaymentTypes.h"

#include <drogon/utils/coroutine.h>

#include <map>
#include <string>

namespace UEAdminAPI {
namespace Services {

// 支付宝：Mode=page 走电脑网站支付（返回收银台 URL），Mode=qr 走订单码/当面付预下单（返回二维码串）。
// 异步通知验签与 alipay.trade.query 查单两种模式共用。
class AlipayProvider {
public:
    static drogon::Task<PayCreateResult> CreateOrder(const std::string &orderNo,
                                                     int64_t amountCents,
                                                     const std::string &subject);

    static drogon::Task<PayQueryResult> Query(const std::string &orderNo);

    // 渠道退款（按商户订单号 + 商户退款单号，幂等）
    static drogon::Task<PayRefundResult> Refund(const std::string &orderNo,
                                                const std::string &refundNo,
                                                int64_t amountCents,
                                                const std::string &reason);

    static drogon::Task<PayRefundResult> QueryRefund(const std::string &orderNo,
                                                     const std::string &refundNo);

    // 解析并验签 application/x-www-form-urlencoded 异步通知。
    static PayNotifyResult ParseNotify(const std::string &body);

private:
    static std::string BuildSignContent(const std::map<std::string, std::string> &params);
    static std::string BuildFormBody(const std::map<std::string, std::string> &params);
};

} // namespace Services
} // namespace UEAdminAPI

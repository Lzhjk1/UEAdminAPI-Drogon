#pragma once

#include <cstdint>
#include <map>
#include <string>

#include <drogon/utils/coroutine.h>

#include "utils/HttpResult.h"

namespace UEAdminAPI {
namespace Services {

struct NotifyReply {
    bool ok = false;
    std::string message;
};

// 余额与支付。渠道 1=支付宝（alipay.trade.precreate），2=微信（Native APIv3），9=模拟。
// 真实渠道的下单/查单走网络调用，因此 CreateOrder / GetOrder 为协程；
// 结算（置为已支付 + 加余额 + 记流水）统一走同一个幂等 CTE，回调与主动查单共用。
class BillService
{
public:
    static drogon::Task<utils::HttpResult> CreateOrder(int userId,
                                                       int64_t amountCents,
                                                       int channel,
                                                       const std::string &idemKey);

    static drogon::Task<utils::HttpResult> GetOrder(int userId, const std::string &orderNo);

    // 我的已支付订单（含最近一条退款申请状态），供客户端发起退款
    static utils::HttpResult ListOrders(int userId, int page, int size);

    // 余额流水（type: 0 全部 1 充值 2 消费 3 退款）
    static utils::HttpResult ListTransactions(int userId, int type, int page, int size);

    // 模拟到账：仅 TestMode 开启或 channel == 9 时可用；幂等（重复调用不重复入账）
    static utils::HttpResult MockPaid(int userId, const std::string &orderNo);

    // 支付宝异步通知（application/x-www-form-urlencoded）
    static NotifyReply HandleAlipayNotify(const std::string &body,
                                          const std::map<std::string, std::string> &headers);

    // 微信支付异步通知（JSON + 验签头）
    static NotifyReply HandleWechatNotify(const std::string &body,
                                          const std::map<std::string, std::string> &headers);

    // 按订单已保存的 qr_content 渲染二维码 PNG；无内容或模拟订单返回 false
    static bool GetQrPng(const std::string &orderNo, std::string &png);
};

} // namespace Services
} // namespace UEAdminAPI

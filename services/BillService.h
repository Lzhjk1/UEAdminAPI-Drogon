#pragma once

#include <cstdint>
#include <string>
#include "utils/HttpResult.h"

namespace UEAdminAPI {
namespace Services {

// 余额与支付。当前为「模拟支付」：下单后可通过 mock-paid 直接到账，
// 真实支付宝当面付 / 微信 Native 接入时只需替换 I/实现 CreateQrContent 与回调验签，
// 订单、流水、余额的计算与幂等逻辑不变。
class BillService
{
public:
    // 创建充值订单（金额单位：分；channel: 1 支付宝 2 微信 9 模拟）
    static utils::HttpResult CreateOrder(int userId,
                                         int64_t amountCents,
                                         int channel,
                                         const std::string &idemKey);

    // 查询订单（仅本人可见）
    static utils::HttpResult GetOrder(int userId, const std::string &orderNo);

    // 余额流水（type: 0 全部 1 充值 2 消费 3 退款）
    static utils::HttpResult ListTransactions(int userId, int type, int page, int size);

    // 模拟到账：仅 TestMode 开启或 channel == 9 时可用；幂等（重复调用不重复入账）
    static utils::HttpResult MockPaid(int userId, const std::string &orderNo);
};

} // namespace Services
} // namespace UEAdminAPI

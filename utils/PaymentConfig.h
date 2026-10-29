#pragma once

#include <json/json.h>
#include <string>

namespace UEAdminAPI {
namespace Services {

struct AlipayPayConfig {
    bool enabled = false;
    std::string appId;
    std::string gateway;
    std::string notifyUrl;
    std::string returnUrl;
    std::string mode;        // page=电脑网站支付；qr=订单码/当面付预下单
    std::string productCode; // page: FAST_INSTANT_TRADE_PAY；qr: QR_CODE_OFFLINE / FACE_TO_FACE_PAYMENT
    std::string privateKeyPem;
    std::string publicKeyPem;
};

struct WechatPayConfig {
    bool enabled = false;
    std::string mchId;
    std::string appId;
    std::string serialNo;      // 商户 API 证书序列号
    std::string apiV3Key;      // 32 位 APIv3 密钥
    std::string publicKeyId;   // 微信支付公钥 ID（PUB_KEY_ID_ 开头）
    std::string privateKeyPem; // apiclient_key.pem
    std::string publicKeyPem;  // 微信支付公钥 pub_key.pem
    std::string notifyUrl;
    std::string apiBase;
};

// 支付渠道配置：只负责读取 custom_config["Payment"]，不发起任何网络请求。
// 凭据为空时渠道视为未就绪，下单会返回 -906，不会退回模拟支付。
class PaymentConfig {
public:
    static void Init(const Json::Value &customConfig);

    static bool Enabled();
    static const AlipayPayConfig &Alipay();
    static const WechatPayConfig &Wechat();

    static bool AlipayReady();
    static bool WechatReady();
    static bool ChannelReady(int channel);  // 1 支付宝 2 微信

private:
    inline static bool _enabled = false;
    inline static AlipayPayConfig _alipay;
    inline static WechatPayConfig _wechat;
};

} // namespace Services
} // namespace UEAdminAPI

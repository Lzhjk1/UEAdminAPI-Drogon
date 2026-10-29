#include "PaymentConfig.h"

#include <trantor/utils/Logger.h>

#include <fstream>
#include <sstream>

namespace UEAdminAPI {
namespace Services {

namespace {

std::string Trim(const std::string &s)
{
    const auto begin = s.find_first_not_of(" \t\r\n\"'");
    if (begin == std::string::npos)
    {
        return "";
    }
    const auto end = s.find_last_not_of(" \t\r\n\"'");
    return s.substr(begin, end - begin + 1);
}

std::string ReadPem(const Json::Value &node, const char *pathKey, const char *pemKey)
{
    const std::string pem = Trim(node.get(pemKey, "").asString());
    if (!pem.empty())
    {
        return pem;
    }
    const std::string path = Trim(node.get(pathKey, "").asString());
    if (path.empty())
    {
        return "";
    }
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
    {
        LOG_ERROR << "[Payment] 无法读取密钥文件: " << path;
        return "";
    }
    std::ostringstream oss;
    oss << in.rdbuf();
    return Trim(oss.str());
}

std::string JoinNotifyUrl(const std::string &serverHost, const std::string &configured, const char *path)
{
    if (!configured.empty())
    {
        return configured;
    }
    std::string host = serverHost;
    while (!host.empty() && (host.back() == '/' || host.back() == ' '))
    {
        host.pop_back();
    }
    return host + path;
}

} // namespace

void PaymentConfig::Init(const Json::Value &customConfig)
{
    _alipay = AlipayPayConfig{};
    _wechat = WechatPayConfig{};
    _enabled = false;

    const std::string serverHost = Trim(customConfig.get("ServerHost", "").asString());

    if (!customConfig.isMember("Payment") || !customConfig["Payment"].isObject())
    {
        LOG_INFO << "[Payment] 未配置 Payment 节点，支付宝/微信支付不可用（渠道 9 模拟支付不受影响）";
        return;
    }

    const auto &payment = customConfig["Payment"];
    _enabled = payment.get("Enabled", false).asBool();

    const auto &alipay = payment["Alipay"];
    _alipay.enabled = alipay.get("Enabled", false).asBool();
    _alipay.appId = Trim(alipay.get("AppId", "").asString());
    _alipay.gateway = Trim(alipay.get("Gateway", "https://openapi.alipay.com/gateway.do").asString());
    _alipay.mode = Trim(alipay.get("Mode", "page").asString());
    if (_alipay.mode != "qr")
    {
        _alipay.mode = "page";
    }
    _alipay.productCode = Trim(alipay.get("ProductCode", "").asString());
    if (_alipay.productCode.empty())
    {
        _alipay.productCode = (_alipay.mode == "qr") ? "QR_CODE_OFFLINE" : "FAST_INSTANT_TRADE_PAY";
    }
    _alipay.returnUrl = Trim(alipay.get("ReturnUrl", "").asString());
    _alipay.privateKeyPem = ReadPem(alipay, "PrivateKeyPath", "PrivateKeyPem");
    _alipay.publicKeyPem = ReadPem(alipay, "PublicKeyPath", "PublicKeyPem");
    _alipay.notifyUrl = JoinNotifyUrl(serverHost, Trim(alipay.get("NotifyUrl", "").asString()), "/api/bill/notify/alipay");

    const auto &wechat = payment["WeChat"];
    _wechat.enabled = wechat.get("Enabled", false).asBool();
    _wechat.mchId = Trim(wechat.get("MchId", "").asString());
    _wechat.appId = Trim(wechat.get("AppId", "").asString());
    _wechat.serialNo = Trim(wechat.get("SerialNo", "").asString());
    _wechat.apiV3Key = Trim(wechat.get("ApiV3Key", "").asString());
    _wechat.publicKeyId = Trim(wechat.get("PublicKeyId", "").asString());
    _wechat.privateKeyPem = ReadPem(wechat, "PrivateKeyPath", "PrivateKeyPem");
    _wechat.publicKeyPem = ReadPem(wechat, "PublicKeyPath", "PublicKeyPem");
    _wechat.notifyUrl = JoinNotifyUrl(serverHost, Trim(wechat.get("NotifyUrl", "").asString()), "/api/bill/notify/wechat");
    _wechat.apiBase = Trim(wechat.get("ApiBase", "https://api.mch.weixin.qq.com").asString());

    if (!_enabled)
    {
        LOG_INFO << "[Payment] Payment.Enabled=false，支付宝/微信支付不可用（渠道 9 模拟支付不受影响）";
        return;
    }

    LOG_INFO << "[Payment] 支付宝: " << (_alipay.enabled ? "启用" : "关闭")
             << ", AppId=" << (_alipay.appId.empty() ? "<未配置>" : _alipay.appId)
             << ", 模式=" << _alipay.mode << "(" << _alipay.productCode << ")"
             << ", 回调=" << _alipay.notifyUrl
             << ", 就绪=" << (AlipayReady() ? "是" : "否");
    LOG_INFO << "[Payment] 微信支付: " << (_wechat.enabled ? "启用" : "关闭")
             << ", MchId=" << (_wechat.mchId.empty() ? "<未配置>" : _wechat.mchId)
             << ", 回调=" << _wechat.notifyUrl
             << ", 就绪=" << (WechatReady() ? "是" : "否");

    if (_alipay.enabled && !AlipayReady())
    {
        LOG_WARN << "[Payment] 支付宝缺少 AppId / 应用私钥 / 支付宝公钥，下单将返回 -906";
    }
    if (_wechat.enabled && !WechatReady())
    {
        LOG_WARN << "[Payment] 微信支付缺少 MchId/AppId/证书序列号/APIv3密钥/商户私钥/微信支付公钥，下单将返回 -906";
    }
}

bool PaymentConfig::Enabled()
{
    return _enabled;
}

const AlipayPayConfig &PaymentConfig::Alipay()
{
    return _alipay;
}

const WechatPayConfig &PaymentConfig::Wechat()
{
    return _wechat;
}

bool PaymentConfig::AlipayReady()
{
    const bool notifyOk = _alipay.notifyUrl.rfind("http://", 0) == 0 || _alipay.notifyUrl.rfind("https://", 0) == 0;
    return _enabled && _alipay.enabled && !_alipay.appId.empty() && !_alipay.gateway.empty() && notifyOk &&
           !_alipay.privateKeyPem.empty() && !_alipay.publicKeyPem.empty();
}

bool PaymentConfig::WechatReady()
{
    const bool notifyOk = _wechat.notifyUrl.rfind("http://", 0) == 0 || _wechat.notifyUrl.rfind("https://", 0) == 0;
    return _enabled && _wechat.enabled && !_wechat.mchId.empty() && !_wechat.appId.empty() &&
           !_wechat.serialNo.empty() && _wechat.apiV3Key.size() == 32 && notifyOk &&
           !_wechat.privateKeyPem.empty() && !_wechat.publicKeyPem.empty() && !_wechat.apiBase.empty();
}

bool PaymentConfig::ChannelReady(int channel)
{
    if (channel == 1)
    {
        return AlipayReady();
    }
    if (channel == 2)
    {
        return WechatReady();
    }
    return false;
}

} // namespace Services
} // namespace UEAdminAPI

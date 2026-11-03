#include "WechatPayProvider.h"

#include "utils/PaymentConfig.h"
#include "utils/PaymentCrypto.h"
#include "utils/PaymentUtils.h"

#include <drogon/HttpClient.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>
#include <trantor/utils/Logger.h>

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <memory>

using namespace drogon;
using namespace UEAdminAPI::utils;

namespace UEAdminAPI {
namespace Services {

namespace {

constexpr const char *kNativePath = "/v3/pay/transactions/native";
constexpr int64_t kNotifyToleranceSec = 300;

std::shared_ptr<HttpClient> GetClient(const std::string &base)
{
    static std::shared_ptr<HttpClient> client = HttpClient::newHttpClient(base);
    return client;
}

std::string Lower(std::string s)
{
    for (char &c : s)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string HeaderValue(const std::map<std::string, std::string> &headers, const std::string &name)
{
    const auto it = headers.find(Lower(name));
    return it == headers.end() ? "" : it->second;
}

// 微信 time_expire 要求 RFC3339，这里按 +08:00 输出，避免服务器时区不是东八区
std::string ExpireAt(int minutes)
{
    const std::time_t t = std::time(nullptr) + static_cast<std::time_t>(minutes) * 60 + 8 * 3600;
    std::tm tmv{};
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    char buf[40] = {0};
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tmv);
    return std::string(buf) + "+08:00";
}

bool ParseJson(const std::string &body, Json::Value &root)
{
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    std::string errs;
    return reader->parse(body.data(), body.data() + body.size(), &root, &errs);
}

std::string ProviderError(const HttpResponsePtr &resp, const std::string &body)
{
    Json::Value root;
    if (ParseJson(body, root))
    {
        const std::string code = root.get("code", "").asString();
        const std::string message = root.get("message", "").asString();
        if (!code.empty() || !message.empty())
        {
            return "微信支付返回 " + code + ": " + message;
        }
    }
    if (resp)
    {
        return "微信支付 HTTP " + std::to_string(resp->statusCode());
    }
    return "微信支付无响应";
}

} // namespace

std::string WechatPayProvider::BuildAuthorization(const std::string &method,
                                                  const std::string &pathWithQuery,
                                                  const std::string &body)
{
    const auto &cfg = PaymentConfig::Wechat();
    const std::string nonce = PaymentCrypto::RandomHex(16);
    const std::string timestamp = std::to_string(static_cast<long long>(std::time(nullptr)));

    const std::string message = method + "\n" + pathWithQuery + "\n" + timestamp + "\n" + nonce + "\n" + body + "\n";
    const std::string signature = PaymentCrypto::RsaSignSha256(message, cfg.privateKeyPem);
    if (signature.empty())
    {
        return "";
    }

    return "WECHATPAY2-SHA256-RSA2048 mchid=\"" + cfg.mchId + "\",nonce_str=\"" + nonce +
           "\",signature=\"" + signature + "\",timestamp=\"" + timestamp + "\",serial_no=\"" + cfg.serialNo + "\"";
}

bool WechatPayProvider::VerifySignature(const std::string &timestamp,
                                        const std::string &nonce,
                                        const std::string &body,
                                        const std::string &signature,
                                        const std::string &serial)
{
    const auto &cfg = PaymentConfig::Wechat();
    if (timestamp.empty() || nonce.empty() || signature.empty())
    {
        LOG_WARN << "[WeChatPay] 回调缺少签名头";
        return false;
    }

    // 公钥模式：serial 为公钥 ID；证书模式未实现，避免误用错误公钥放行
    if (!cfg.publicKeyId.empty() && serial != cfg.publicKeyId)
    {
        LOG_WARN << "[WeChatPay] 回调公钥 ID 不匹配: " << serial;
        return false;
    }
    if (!serial.empty() && serial.rfind("PUB_KEY_ID_", 0) != 0)
    {
        LOG_WARN << "[WeChatPay] 收到平台证书序列号 " << serial << "，当前仅配置公钥模式";
        return false;
    }

    const std::string message = timestamp + "\n" + nonce + "\n" + body + "\n";
    return PaymentCrypto::RsaVerifySha256(message, signature, cfg.publicKeyPem);
}

Task<PayCreateResult> WechatPayProvider::CreateNative(const std::string &orderNo,
                                                      int64_t amountCents,
                                                      const std::string &description)
{
    PayCreateResult result;
    const auto &cfg = PaymentConfig::Wechat();
    if (!PaymentConfig::WechatReady())
    {
        result.error = "微信支付渠道未配置";
        co_return result;
    }

    Json::Value body;
    body["appid"] = cfg.appId;
    body["mchid"] = cfg.mchId;
    body["description"] = description;
    body["out_trade_no"] = orderNo;
    body["notify_url"] = cfg.notifyUrl;
    body["time_expire"] = ExpireAt(30);
    body["amount"]["total"] = static_cast<Json::Int64>(amountCents);
    body["amount"]["currency"] = "CNY";
    const std::string payload = PaymentUtils::JsonCompact(body);

    const std::string authorization = BuildAuthorization("POST", kNativePath, payload);
    if (authorization.empty())
    {
        result.error = "微信支付下单签名失败";
        co_return result;
    }

    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Post);
    req->setPath(kNativePath);
    req->setBody(payload);
    req->setContentTypeString("application/json");
    req->addHeader("Accept", "application/json");
    req->addHeader("Authorization", authorization);

    HttpResponsePtr resp;
    try
    {
        resp = co_await GetClient(cfg.apiBase)->sendRequestCoro(req);
    }
    catch (const std::exception &e)
    {
        result.error = std::string("微信支付请求异常: ") + e.what();
        LOG_ERROR << "[WeChatPay] " << result.error;
        co_return result;
    }

    const std::string respBody = resp ? std::string(resp->body().data(), resp->body().size()) : "";
    if (!resp || resp->statusCode() != k200OK)
    {
        result.error = ProviderError(resp, respBody);
        LOG_ERROR << "[WeChatPay] 下单失败 orderNo=" << orderNo << ", " << result.error;
        co_return result;
    }

    Json::Value root;
    if (!ParseJson(respBody, root))
    {
        result.error = "微信支付响应不是合法 JSON";
        co_return result;
    }
    result.qrContent = root.get("code_url", "").asString();
    if (result.qrContent.empty())
    {
        result.error = "微信支付未返回 code_url";
        co_return result;
    }
    result.ok = true;
    LOG_INFO << "[WeChatPay] Native 下单成功 orderNo=" << orderNo << ", amount=" << amountCents;
    co_return result;
}

Task<PayQueryResult> WechatPayProvider::Query(const std::string &orderNo)
{
    PayQueryResult result;
    const auto &cfg = PaymentConfig::Wechat();
    if (!PaymentConfig::WechatReady())
    {
        result.error = "微信支付渠道未配置";
        co_return result;
    }

    const std::string path = std::string("/v3/pay/transactions/out-trade-no/") +
                             drogon::utils::urlEncode(orderNo) + "?mchid=" + drogon::utils::urlEncode(cfg.mchId);
    const std::string authorization = BuildAuthorization("GET", path, "");
    if (authorization.empty())
    {
        result.error = "微信支付查单签名失败";
        co_return result;
    }

    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Get);
    req->setPath(path);
    req->addHeader("Accept", "application/json");
    req->addHeader("Authorization", authorization);

    HttpResponsePtr resp;
    try
    {
        resp = co_await GetClient(cfg.apiBase)->sendRequestCoro(req);
    }
    catch (const std::exception &e)
    {
        result.error = std::string("微信支付查单异常: ") + e.what();
        co_return result;
    }

    const std::string body = resp ? std::string(resp->body().data(), resp->body().size()) : "";
    if (resp && resp->statusCode() == k404NotFound)
    {
        result.ok = true;
        result.state = "NOT_EXIST";
        co_return result;
    }
    if (!resp || resp->statusCode() != k200OK)
    {
        result.error = ProviderError(resp, body);
        co_return result;
    }

    Json::Value root;
    if (!ParseJson(body, root))
    {
        result.error = "微信支付查单响应不是合法 JSON";
        co_return result;
    }
    result.state = root.get("trade_state", "").asString();
    result.providerTradeNo = root.get("transaction_id", "").asString();
    const auto &amount = root["amount"];
    if (amount.isObject() && (amount["total"].isInt64() || amount["total"].isInt()))
    {
        result.paidCents = amount["total"].asInt64();
    }
    result.ok = true;
    result.paid = (result.state == "SUCCESS");
    result.closed = (result.state == "CLOSED" || result.state == "REVOKED" || result.state == "PAYERROR");
    co_return result;
}

Task<PayCloseResult> WechatPayProvider::Close(const std::string &orderNo)
{
    PayCloseResult result;
    const auto &cfg = PaymentConfig::Wechat();
    if (!PaymentConfig::WechatReady())
    {
        result.error = "微信支付渠道未配置";
        co_return result;
    }

    const std::string path = std::string("/v3/pay/transactions/out-trade-no/") +
                             drogon::utils::urlEncode(orderNo) + "/close";
    Json::Value body;
    body["mchid"] = cfg.mchId;
    const std::string payload = PaymentUtils::JsonCompact(body);

    const std::string authorization = BuildAuthorization("POST", path, payload);
    if (authorization.empty())
    {
        result.error = "微信支付关单签名失败";
        co_return result;
    }

    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Post);
    req->setPath(path);
    req->setBody(payload);
    req->setContentTypeString("application/json");
    req->addHeader("Accept", "application/json");
    req->addHeader("Authorization", authorization);

    HttpResponsePtr resp;
    try
    {
        resp = co_await GetClient(cfg.apiBase)->sendRequestCoro(req);
    }
    catch (const std::exception &e)
    {
        result.error = std::string("微信支付关单异常: ") + e.what();
        LOG_ERROR << "[WeChatPay] " << result.error;
        co_return result;
    }

    if (resp && (resp->statusCode() == k204NoContent || resp->statusCode() == k200OK))
    {
        result.ok = true;
        co_return result;
    }

    const std::string respBody = resp ? std::string(resp->body().data(), resp->body().size()) : "";
    Json::Value root;
    const std::string code = ParseJson(respBody, root) ? root.get("code", "").asString() : "";
    result.state = code;
    if (code == "ORDER_NOT_EXIST" || code == "ORDER_CLOSED")
    {
        result.ok = true;
        co_return result;
    }
    if (code == "ORDERPAID")
    {
        result.alreadyPaid = true;
        co_return result;
    }
    result.error = ProviderError(resp, respBody);
    LOG_WARN << "[WeChatPay] 关单失败 orderNo=" << orderNo << ", " << result.error;
    co_return result;
}

Task<PayRefundResult> WechatPayProvider::Refund(const std::string &orderNo,
                                                const std::string &refundNo,
                                                int64_t refundCents,
                                                int64_t totalCents,
                                                const std::string &reason)
{
    PayRefundResult result;
    const auto &cfg = PaymentConfig::Wechat();
    if (!PaymentConfig::WechatReady())
    {
        result.error = "微信支付渠道未配置";
        co_return result;
    }

    Json::Value body;
    body["out_trade_no"] = orderNo;
    body["out_refund_no"] = refundNo;
    body["reason"] = reason.substr(0, 80);
    body["amount"]["refund"] = static_cast<Json::Int64>(refundCents);
    body["amount"]["total"] = static_cast<Json::Int64>(totalCents);
    body["amount"]["currency"] = "CNY";
    const std::string payload = PaymentUtils::JsonCompact(body);

    constexpr const char *kRefundPath = "/v3/refund/domestic/refunds";
    const std::string authorization = BuildAuthorization("POST", kRefundPath, payload);
    if (authorization.empty())
    {
        result.error = "微信支付退款签名失败";
        co_return result;
    }

    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Post);
    req->setPath(kRefundPath);
    req->setBody(payload);
    req->setContentTypeString("application/json");
    req->addHeader("Accept", "application/json");
    req->addHeader("Authorization", authorization);

    HttpResponsePtr resp;
    try
    {
        resp = co_await GetClient(cfg.apiBase)->sendRequestCoro(req);
    }
    catch (const std::exception &e)
    {
        result.error = std::string("微信支付退款异常: ") + e.what();
        LOG_ERROR << "[WeChatPay] " << result.error;
        co_return result;
    }

    const std::string respBody = resp ? std::string(resp->body().data(), resp->body().size()) : "";
    if (!resp || resp->statusCode() != k200OK)
    {
        result.error = ProviderError(resp, respBody);
        LOG_ERROR << "[WeChatPay] 退款失败 orderNo=" << orderNo << ", " << result.error;
        co_return result;
    }

    Json::Value root;
    if (!ParseJson(respBody, root))
    {
        result.error = "微信支付退款响应不是合法 JSON";
        co_return result;
    }
    result.ok = true;
    result.state = root.get("status", "").asString();
    result.providerRefundNo = root.get("refund_id", "").asString();
    result.success = (result.state == "SUCCESS");
    result.processing = (result.state == "PROCESSING");
    if (!result.success && !result.processing)
    {
        result.error = "微信支付退款状态: " + result.state;
    }
    LOG_INFO << "[WeChatPay] 退款受理 orderNo=" << orderNo << " refundNo=" << refundNo
             << " status=" << result.state;
    co_return result;
}

Task<PayRefundResult> WechatPayProvider::QueryRefund(const std::string &refundNo)
{
    PayRefundResult result;
    const auto &cfg = PaymentConfig::Wechat();
    if (!PaymentConfig::WechatReady())
    {
        result.error = "微信支付渠道未配置";
        co_return result;
    }

    const std::string path = std::string("/v3/refund/domestic/refunds/") + drogon::utils::urlEncode(refundNo);
    const std::string authorization = BuildAuthorization("GET", path, "");
    if (authorization.empty())
    {
        result.error = "微信支付退款查询签名失败";
        co_return result;
    }

    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Get);
    req->setPath(path);
    req->addHeader("Accept", "application/json");
    req->addHeader("Authorization", authorization);

    HttpResponsePtr resp;
    try
    {
        resp = co_await GetClient(cfg.apiBase)->sendRequestCoro(req);
    }
    catch (const std::exception &e)
    {
        result.error = std::string("微信支付退款查询异常: ") + e.what();
        co_return result;
    }

    const std::string body = resp ? std::string(resp->body().data(), resp->body().size()) : "";
    if (!resp || resp->statusCode() != k200OK)
    {
        result.error = ProviderError(resp, body);
        co_return result;
    }

    Json::Value root;
    if (!ParseJson(body, root))
    {
        result.error = "微信支付退款查询响应不是合法 JSON";
        co_return result;
    }
    result.ok = true;
    result.state = root.get("status", "").asString();
    result.providerRefundNo = root.get("refund_id", "").asString();
    result.success = (result.state == "SUCCESS");
    result.processing = (result.state == "PROCESSING");
    co_return result;
}

PayNotifyResult WechatPayProvider::ParseNotify(const std::map<std::string, std::string> &headers,
                                               const std::string &body)
{
    PayNotifyResult result;
    const auto &cfg = PaymentConfig::Wechat();
    if (!PaymentConfig::WechatReady())
    {
        result.error = "微信支付渠道未配置";
        return result;
    }

    const std::string timestamp = HeaderValue(headers, "Wechatpay-Timestamp");
    const std::string nonce = HeaderValue(headers, "Wechatpay-Nonce");
    const std::string signature = HeaderValue(headers, "Wechatpay-Signature");
    const std::string serial = HeaderValue(headers, "Wechatpay-Serial");

    long long ts = 0;
    try
    {
        ts = std::stoll(timestamp);
    }
    catch (...)
    {
        result.error = "微信支付回调时间戳不合法";
        return result;
    }
    const long long now = static_cast<long long>(std::time(nullptr));
    if (std::llabs(now - ts) > kNotifyToleranceSec)
    {
        result.error = "微信支付回调已过期";
        return result;
    }

    if (!VerifySignature(timestamp, nonce, body, signature, serial))
    {
        result.error = "微信支付回调验签失败";
        return result;
    }

    Json::Value root;
    if (!ParseJson(body, root))
    {
        result.error = "微信支付回调不是合法 JSON";
        return result;
    }

    const std::string eventType = root.get("event_type", "").asString();
    if (eventType != "TRANSACTION.SUCCESS")
    {
        // 非支付成功事件直接确认，避免微信重复推送
        result.ok = true;
        result.state = eventType;
        return result;
    }

    const auto &resource = root["resource"];
    if (!resource.isObject())
    {
        result.error = "微信支付回调缺少 resource";
        return result;
    }
    const std::string algorithm = resource.get("algorithm", "").asString();
    if (algorithm != "AEAD_AES_256_GCM")
    {
        result.error = "微信支付回调不支持的加密算法: " + algorithm;
        return result;
    }

    std::string plaintext;
    if (!PaymentCrypto::AesGcmDecrypt(cfg.apiV3Key,
                                      resource.get("nonce", "").asString(),
                                      resource.get("associated_data", "").asString(),
                                      drogon::utils::base64Decode(resource.get("ciphertext", "").asString()),
                                      plaintext))
    {
        result.error = "微信支付回调解密失败";
        return result;
    }

    Json::Value payload;
    if (!ParseJson(plaintext, payload))
    {
        result.error = "微信支付回调解密后不是合法 JSON";
        return result;
    }

    if (payload.get("mchid", "").asString() != cfg.mchId || payload.get("appid", "").asString() != cfg.appId)
    {
        result.error = "微信支付回调商户号/APPID 不匹配";
        return result;
    }

    result.orderNo = payload.get("out_trade_no", "").asString();
    result.providerTradeNo = payload.get("transaction_id", "").asString();
    result.state = payload.get("trade_state", "").asString();
    const auto &amount = payload["amount"];
    if (amount.isObject() && (amount["total"].isInt64() || amount["total"].isInt()))
    {
        result.paidCents = amount["total"].asInt64();
    }
    if (result.orderNo.empty())
    {
        result.error = "微信支付回调缺少 out_trade_no";
        return result;
    }
    result.paid = (result.state == "SUCCESS");
    result.ok = true;
    return result;
}

} // namespace Services
} // namespace UEAdminAPI

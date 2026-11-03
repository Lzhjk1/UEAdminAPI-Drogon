#include "AlipayProvider.h"

#include "utils/PaymentConfig.h"
#include "utils/PaymentCrypto.h"
#include "utils/PaymentUtils.h"

#include <drogon/HttpClient.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/utils/Utilities.h>
#include <json/json.h>
#include <trantor/utils/Logger.h>

#include <ctime>
#include <memory>
#include <sstream>

using namespace drogon;
using namespace UEAdminAPI::utils;

namespace UEAdminAPI {
namespace Services {

namespace {

constexpr const char *kMethodPrecreate = "alipay.trade.precreate";
constexpr const char *kMethodPagePay = "alipay.trade.page.pay";
constexpr const char *kMethodQuery = "alipay.trade.query";
constexpr const char *kMethodClose = "alipay.trade.close";
constexpr const char *kMethodRefund = "alipay.trade.refund";
constexpr const char *kMethodRefundQuery = "alipay.trade.fastpay.refund.query";

std::string NowTimestamp()
{
    std::time_t now = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    char buf[32] = {0};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
    return std::string(buf);
}

std::shared_ptr<HttpClient> GetClient(const std::string &base)
{
    static std::shared_ptr<HttpClient> client = HttpClient::newHttpClient(base);
    return client;
}

bool ParseResponse(const HttpResponsePtr &resp, const char *nodeName, Json::Value &node, std::string &error)
{
    if (!resp)
    {
        error = "支付宝无响应";
        return false;
    }
    const std::string body(resp->body().data(), resp->body().size());
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    Json::Value root;
    std::string errs;
    if (!reader->parse(body.data(), body.data() + body.size(), &root, &errs))
    {
        error = "支付宝响应不是合法 JSON";
        LOG_ERROR << "[Alipay] 响应解析失败: " << errs << ", body=" << body.substr(0, 512);
        return false;
    }
    if (!root.isMember(nodeName) || !root[nodeName].isObject())
    {
        error = "支付宝响应缺少 " + std::string(nodeName);
        LOG_ERROR << "[Alipay] " << error << ", body=" << body.substr(0, 512);
        return false;
    }
    node = root[nodeName];
    const std::string code = node.get("code", "").asString();
    const std::string msg = node.get("msg", "").asString();
    if (code != "10000")
    {
        const std::string subCode = node.get("sub_code", "").asString();
        const std::string subMsg = node.get("sub_msg", "").asString();
        error = "支付宝返回 " + code + "/" + subCode + ": " + (subMsg.empty() ? msg : subMsg);
        // 用户尚未扫码时查单会返回 ACQ.TRADE_NOT_EXIST，属于正常待支付
        if (subCode != "ACQ.TRADE_NOT_EXIST")
        {
            LOG_ERROR << "[Alipay] " << error;
        }
        return false;
    }
    return true;
}

} // namespace

std::string AlipayProvider::BuildSignContent(const std::map<std::string, std::string> &params)
{
    std::string content;
    for (const auto &[key, value] : params)
    {
        if (key == "sign" || value.empty())
        {
            continue;
        }
        if (!content.empty())
        {
            content.push_back('&');
        }
        content += key;
        content.push_back('=');
        content += value;
    }
    return content;
}

std::string AlipayProvider::BuildFormBody(const std::map<std::string, std::string> &params)
{
    std::string body;
    for (const auto &[key, value] : params)
    {
        if (!body.empty())
        {
            body.push_back('&');
        }
        body += drogon::utils::urlEncode(key);
        body.push_back('=');
        body += drogon::utils::urlEncode(value);
    }
    return body;
}

Task<PayCreateResult> AlipayProvider::CreateOrder(const std::string &orderNo,
                                                  int64_t amountCents,
                                                  const std::string &subject)
{
    PayCreateResult result;
    const auto &cfg = PaymentConfig::Alipay();
    if (!PaymentConfig::AlipayReady())
    {
        result.error = "支付宝渠道未配置";
        co_return result;
    }

    Json::Value biz;
    biz["out_trade_no"] = orderNo;
    biz["total_amount"] = PaymentUtils::FormatYuan(amountCents);
    biz["subject"] = subject;
    biz["product_code"] = cfg.productCode;
    biz["timeout_express"] = "30m";  // 与 payment_order 的 30 分钟有效期对齐

    const bool qrMode = (cfg.mode == "qr");
    std::map<std::string, std::string> params;
    params["app_id"] = cfg.appId;
    params["method"] = qrMode ? kMethodPrecreate : kMethodPagePay;
    params["format"] = "JSON";
    params["charset"] = "utf-8";
    params["sign_type"] = "RSA2";
    params["timestamp"] = NowTimestamp();
    params["version"] = "1.0";
    params["notify_url"] = cfg.notifyUrl;
    if (!qrMode && !cfg.returnUrl.empty())
    {
        params["return_url"] = cfg.returnUrl;
    }
    params["biz_content"] = PaymentUtils::JsonCompact(biz);

    const std::string signContent = BuildSignContent(params);
    params["sign"] = PaymentCrypto::RsaSignSha256(signContent, cfg.privateKeyPem);
    if (params["sign"].empty())
    {
        result.error = "支付宝下单签名失败";
        co_return result;
    }

    // 电脑网站支付：不需要预下单请求，签名后拼出收银台 URL 交给浏览器打开
    if (!qrMode)
    {
        result.payUrl = cfg.gateway + (cfg.gateway.find('?') == std::string::npos ? "?" : "&") + BuildFormBody(params);
        result.ok = true;
        LOG_INFO << "[Alipay] 电脑网站支付下单成功 orderNo=" << orderNo << ", amount=" << amountCents;
        co_return result;
    }

    std::string base, path;
    if (!PaymentUtils::SplitUrl(cfg.gateway, base, path))
    {
        result.error = "支付宝网关地址不合法";
        co_return result;
    }

    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Post);
    req->setPath(path);
    req->setBody(BuildFormBody(params));
    req->setContentTypeString("application/x-www-form-urlencoded;charset=utf-8");

    HttpResponsePtr resp;
    try
    {
        resp = co_await GetClient(base)->sendRequestCoro(req);
    }
    catch (const std::exception &e)
    {
        result.error = std::string("支付宝请求异常: ") + e.what();
        LOG_ERROR << "[Alipay] " << result.error;
        co_return result;
    }

    Json::Value node;
    if (!ParseResponse(resp, "alipay_trade_precreate_response", node, result.error))
    {
        co_return result;
    }

    result.qrContent = node.get("qr_code", "").asString();
    if (result.qrContent.empty())
    {
        result.error = "支付宝未返回 qr_code";
        LOG_ERROR << "[Alipay] 下单成功但 qr_code 为空, orderNo=" << orderNo;
        co_return result;
    }
    result.ok = true;
    LOG_INFO << "[Alipay] 预下单成功 orderNo=" << orderNo << ", amount=" << amountCents;
    co_return result;
}

Task<PayQueryResult> AlipayProvider::Query(const std::string &orderNo)
{
    PayQueryResult result;
    const auto &cfg = PaymentConfig::Alipay();
    if (!PaymentConfig::AlipayReady())
    {
        result.error = "支付宝渠道未配置";
        co_return result;
    }

    Json::Value biz;
    biz["out_trade_no"] = orderNo;

    std::map<std::string, std::string> params;
    params["app_id"] = cfg.appId;
    params["method"] = kMethodQuery;
    params["format"] = "JSON";
    params["charset"] = "utf-8";
    params["sign_type"] = "RSA2";
    params["timestamp"] = NowTimestamp();
    params["version"] = "1.0";
    params["biz_content"] = PaymentUtils::JsonCompact(biz);

    params["sign"] = PaymentCrypto::RsaSignSha256(BuildSignContent(params), cfg.privateKeyPem);
    if (params["sign"].empty())
    {
        result.error = "支付宝查单签名失败";
        co_return result;
    }

    std::string base, path;
    if (!PaymentUtils::SplitUrl(cfg.gateway, base, path))
    {
        result.error = "支付宝网关地址不合法";
        co_return result;
    }

    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Post);
    req->setPath(path);
    req->setBody(BuildFormBody(params));
    req->setContentTypeString("application/x-www-form-urlencoded;charset=utf-8");

    HttpResponsePtr resp;
    try
    {
        resp = co_await GetClient(base)->sendRequestCoro(req);
    }
    catch (const std::exception &e)
    {
        result.error = std::string("支付宝查单异常: ") + e.what();
        co_return result;
    }

    Json::Value node;
    if (!ParseResponse(resp, "alipay_trade_query_response", node, result.error))
    {
        // 用户尚未扫码时支付宝会返回 ACQ.TRADE_NOT_EXIST，不算失败
        const std::string code = node.get("sub_code", "").asString();
        if (code == "ACQ.TRADE_NOT_EXIST")
        {
            result.ok = true;
            result.state = "NOT_EXIST";
            result.error.clear();
        }
        co_return result;
    }

    result.state = node.get("trade_status", "").asString();
    result.providerTradeNo = node.get("trade_no", "").asString();
    int64_t cents = 0;
    if (PaymentUtils::ParseYuanToCents(node.get("total_amount", "").asString(), cents))
    {
        result.paidCents = cents;
    }
    result.ok = true;
    result.paid = (result.state == "TRADE_SUCCESS" || result.state == "TRADE_FINISHED");
    result.closed = (result.state == "TRADE_CLOSED");
    co_return result;
}

Task<PayCloseResult> AlipayProvider::Close(const std::string &orderNo)
{
    PayCloseResult result;
    const auto &cfg = PaymentConfig::Alipay();
    if (!PaymentConfig::AlipayReady())
    {
        result.error = "支付宝渠道未配置";
        co_return result;
    }

    Json::Value biz;
    biz["out_trade_no"] = orderNo;

    std::map<std::string, std::string> params;
    params["app_id"] = cfg.appId;
    params["method"] = kMethodClose;
    params["format"] = "JSON";
    params["charset"] = "utf-8";
    params["sign_type"] = "RSA2";
    params["timestamp"] = NowTimestamp();
    params["version"] = "1.0";
    params["biz_content"] = PaymentUtils::JsonCompact(biz);
    params["sign"] = PaymentCrypto::RsaSignSha256(BuildSignContent(params), cfg.privateKeyPem);
    if (params["sign"].empty())
    {
        result.error = "支付宝关单签名失败";
        co_return result;
    }

    std::string base, path;
    if (!PaymentUtils::SplitUrl(cfg.gateway, base, path))
    {
        result.error = "支付宝网关地址不合法";
        co_return result;
    }

    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Post);
    req->setPath(path);
    req->setBody(BuildFormBody(params));
    req->setContentTypeString("application/x-www-form-urlencoded;charset=utf-8");

    HttpResponsePtr resp;
    try
    {
        resp = co_await GetClient(base)->sendRequestCoro(req);
    }
    catch (const std::exception &e)
    {
        result.error = std::string("支付宝关单异常: ") + e.what();
        LOG_ERROR << "[Alipay] " << result.error;
        co_return result;
    }

    if (!resp)
    {
        result.error = "支付宝无响应";
        co_return result;
    }

    const std::string body(resp->body().data(), resp->body().size());
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    Json::Value root;
    std::string errs;
    if (!reader->parse(body.data(), body.data() + body.size(), &root, &errs) ||
        !root.isMember("alipay_trade_close_response") || !root["alipay_trade_close_response"].isObject())
    {
        result.error = "支付宝关单响应不是合法 JSON";
        LOG_ERROR << "[Alipay] 关单响应解析失败: " << errs << ", body=" << body.substr(0, 512);
        co_return result;
    }

    const Json::Value &node = root["alipay_trade_close_response"];
    const std::string code = node.get("code", "").asString();
    const std::string subCode = node.get("sub_code", "").asString();
    result.state = code + "/" + subCode;
    if (code == "10000")
    {
        result.ok = true;
        co_return result;
    }
    if (subCode == "ACQ.TRADE_NOT_EXIST")
    {
        // 电脑网站支付是本地签名 URL，用户未打开收银台时渠道侧没有订单
        result.ok = true;
        co_return result;
    }
    if (subCode == "ACQ.TRADE_STATUS_ERROR" || subCode == "ACQ.TRADE_HAS_FINISHED")
    {
        result.alreadyPaid = true;
        co_return result;
    }
    result.error = "支付宝关单返回 " + code + "/" + subCode + ": " + node.get("sub_msg", "").asString();
    LOG_WARN << "[Alipay] 关单失败 orderNo=" << orderNo << ", " << result.error;
    co_return result;
}

PayNotifyResult AlipayProvider::ParseNotify(const std::string &body)
{
    PayNotifyResult result;
    const auto &cfg = PaymentConfig::Alipay();
    if (!PaymentConfig::AlipayReady())
    {
        result.error = "支付宝渠道未配置";
        return result;
    }

    const auto params = PaymentUtils::ParseForm(body);
    const auto signIt = params.find("sign");
    if (signIt == params.end() || signIt->second.empty())
    {
        result.error = "支付宝通知缺少 sign";
        return result;
    }
    const auto signTypeIt = params.find("sign_type");
    if (signTypeIt != params.end() && signTypeIt->second != "RSA2")
    {
        result.error = "支付宝通知签名类型不是 RSA2";
        return result;
    }

    // 验签需排除 sign / sign_type，其余非空参数按 key 升序拼接
    std::map<std::string, std::string> signParams;
    for (const auto &[key, value] : params)
    {
        if (key == "sign" || key == "sign_type" || value.empty())
        {
            continue;
        }
        signParams[key] = value;
    }
    const std::string signContent = BuildSignContent(signParams);
    if (!PaymentCrypto::RsaVerifySha256(signContent, signIt->second, cfg.publicKeyPem))
    {
        result.error = "支付宝通知验签失败";
        return result;
    }

    const auto appIt = params.find("app_id");
    if (appIt == params.end() || appIt->second != cfg.appId)
    {
        result.error = "支付宝通知 app_id 不匹配";
        return result;
    }

    result.orderNo = params.count("out_trade_no") ? params.at("out_trade_no") : "";
    result.providerTradeNo = params.count("trade_no") ? params.at("trade_no") : "";
    result.state = params.count("trade_status") ? params.at("trade_status") : "";
    if (result.orderNo.empty())
    {
        result.error = "支付宝通知缺少 out_trade_no";
        return result;
    }
    if (!PaymentUtils::ParseYuanToCents(params.count("total_amount") ? params.at("total_amount") : "",
                                        result.paidCents))
    {
        result.error = "支付宝通知 total_amount 不合法";
        return result;
    }

    result.paid = (result.state == "TRADE_SUCCESS" || result.state == "TRADE_FINISHED");
    result.ok = true;
    return result;
}

Task<PayRefundResult> AlipayProvider::Refund(const std::string &orderNo,
                                             const std::string &refundNo,
                                             int64_t amountCents,
                                             const std::string &reason)
{
    PayRefundResult result;
    const auto &cfg = PaymentConfig::Alipay();
    if (!PaymentConfig::AlipayReady())
    {
        result.error = "支付宝渠道未配置";
        co_return result;
    }

    Json::Value biz;
    biz["out_trade_no"] = orderNo;
    biz["out_request_no"] = refundNo;
    biz["refund_amount"] = PaymentUtils::FormatYuan(amountCents);
    if (!reason.empty())
    {
        biz["refund_reason"] = reason;
    }

    std::map<std::string, std::string> params;
    params["app_id"] = cfg.appId;
    params["method"] = kMethodRefund;
    params["format"] = "JSON";
    params["charset"] = "utf-8";
    params["sign_type"] = "RSA2";
    params["timestamp"] = NowTimestamp();
    params["version"] = "1.0";
    params["biz_content"] = PaymentUtils::JsonCompact(biz);
    params["sign"] = PaymentCrypto::RsaSignSha256(BuildSignContent(params), cfg.privateKeyPem);
    if (params["sign"].empty())
    {
        result.error = "支付宝退款签名失败";
        co_return result;
    }

    std::string base, path;
    if (!PaymentUtils::SplitUrl(cfg.gateway, base, path))
    {
        result.error = "支付宝网关地址不合法";
        co_return result;
    }

    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Post);
    req->setPath(path);
    req->setBody(BuildFormBody(params));
    req->setContentTypeString("application/x-www-form-urlencoded;charset=utf-8");

    HttpResponsePtr resp;
    try
    {
        resp = co_await GetClient(base)->sendRequestCoro(req);
    }
    catch (const std::exception &e)
    {
        result.error = std::string("支付宝退款请求异常: ") + e.what();
        LOG_ERROR << "[Alipay] " << result.error;
        co_return result;
    }

    Json::Value node;
    if (!ParseResponse(resp, "alipay_trade_refund_response", node, result.error))
    {
        co_return result;
    }

    result.ok = true;
    result.success = true;
    result.state = node.get("fund_change", "").asString();
    result.providerRefundNo = node.get("trade_no", "").asString();
    LOG_INFO << "[Alipay] 退款成功 orderNo=" << orderNo << " refundNo=" << refundNo
             << " amount=" << amountCents << " fundChange=" << result.state;
    co_return result;
}

Task<PayRefundResult> AlipayProvider::QueryRefund(const std::string &orderNo,
                                                  const std::string &refundNo)
{
    PayRefundResult result;
    const auto &cfg = PaymentConfig::Alipay();
    if (!PaymentConfig::AlipayReady())
    {
        result.error = "支付宝渠道未配置";
        co_return result;
    }

    Json::Value biz;
    biz["out_trade_no"] = orderNo;
    biz["out_request_no"] = refundNo;

    std::map<std::string, std::string> params;
    params["app_id"] = cfg.appId;
    params["method"] = kMethodRefundQuery;
    params["format"] = "JSON";
    params["charset"] = "utf-8";
    params["sign_type"] = "RSA2";
    params["timestamp"] = NowTimestamp();
    params["version"] = "1.0";
    params["biz_content"] = PaymentUtils::JsonCompact(biz);
    params["sign"] = PaymentCrypto::RsaSignSha256(BuildSignContent(params), cfg.privateKeyPem);
    if (params["sign"].empty())
    {
        result.error = "支付宝退款查询签名失败";
        co_return result;
    }

    std::string base, path;
    if (!PaymentUtils::SplitUrl(cfg.gateway, base, path))
    {
        result.error = "支付宝网关地址不合法";
        co_return result;
    }

    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Post);
    req->setPath(path);
    req->setBody(BuildFormBody(params));
    req->setContentTypeString("application/x-www-form-urlencoded;charset=utf-8");

    HttpResponsePtr resp;
    try
    {
        resp = co_await GetClient(base)->sendRequestCoro(req);
    }
    catch (const std::exception &e)
    {
        result.error = std::string("支付宝退款查询异常: ") + e.what();
        co_return result;
    }

    Json::Value node;
    if (!ParseResponse(resp, "alipay_trade_fastpay_refund_query_response", node, result.error))
    {
        co_return result;
    }

    result.ok = true;
    result.state = node.get("refund_status", "").asString();
    result.success = (result.state == "REFUND_SUCCESS");
    result.processing = !result.success;
    result.providerRefundNo = node.get("trade_no", "").asString();
    co_return result;
}

} // namespace Services
} // namespace UEAdminAPI

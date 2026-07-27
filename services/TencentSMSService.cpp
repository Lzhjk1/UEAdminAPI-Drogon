#include "TencentSMSService.h"
#include <drogon/drogon.h>
#include <format>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <chrono>
#include <trantor/utils/Logger.h>

using namespace std;

// =====================================================================
// 腾讯云短信 HTTP 实现（TC3-HMAC-SHA256 签名）
// 不依赖 Tencent SDK，全平台通用
// 参考文档: https://cloud.tencent.com/document/api/382/55981
// =====================================================================

namespace {

/// @brief 字节数组转十六进制字符串
string bytesToHex(const unsigned char* data, size_t len) {
    ostringstream oss;
    for (size_t i = 0; i < len; ++i)
        oss << hex << setw(2) << setfill('0') << (int)data[i];
    return oss.str();
}

/// @brief SHA256 哈希，输出十六进制字符串
string sha256Hex(const string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char*)data.data(), data.size(), hash);
    return bytesToHex(hash, SHA256_DIGEST_LENGTH);
}

/// @brief HMAC-SHA256 签名，输出十六进制字符串
string hmacSha256Hex(const string& key, const string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH * 2] = {0};
    unsigned int len = SHA256_DIGEST_LENGTH;
    HMAC(EVP_sha256(), key.data(), (int)key.size(),
         (const unsigned char*)data.data(), data.size(),
         hash, &len);
    return bytesToHex(hash, len);
}

/// @brief HMAC-SHA256 签名，输出原始字节字符串
string hmacSha256Raw(const string& key, const string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH] = {0};
    unsigned int len = SHA256_DIGEST_LENGTH;
    HMAC(EVP_sha256(), key.data(), (int)key.size(),
         (const unsigned char*)data.data(), data.size(),
         hash, &len);
    return string((char*)hash, len);
}

} // anonymous namespace

// =====================================================================
// TC3-HMAC-SHA256 签名
// =====================================================================

string TencentSMSService::sign(const string& secretKey, const string& date,
                               const string& service, const string& stringToSign) const {
    // SignKey = HMAC_SHA256(HMAC_SHA256(HMAC_SHA256("TC3" + secretKey, date), service), "tc3_request")
    auto secretDate = hmacSha256Raw(date, "TC3" + secretKey);
    auto secretService = hmacSha256Raw(service, secretDate);
    auto signKey = hmacSha256Raw("tc3_request", secretService);
    return hmacSha256Hex(signKey, stringToSign);
}

// =====================================================================

TencentSMSService::TencentSMSService(const Json::Value& config) {
    _secretId = config["SMS"]["Tencent"]["Secret"]["Id"].asString();
    _secretKey = config["SMS"]["Tencent"]["Secret"]["Key"].asString();
    _region = config["SMS"]["Tencent"]["Region"].asString();
    _smsSdkAppId = config["SMS"]["Tencent"]["SmsSdkApp"]["Id"].asString();
    _signName = config["SMS"]["Tencent"]["SignName"].asString();

    auto templates = config["SMS"]["Tencent"]["TemplateIds"];
    for (auto& key : templates.getMemberNames()) {
        _templateIds[stringToMFAType(key)] = templates[key].asString();
    }

    LOG_INFO << "TencentSMSService initialized (HTTP mode)";
}

/// @brief 发送短信
/// 构造 TC3-HMAC-SHA256 签名，通过 HTTP POST 调用腾讯云 SMS API
/// @param phoneNumber 手机号（不含 +86 前缀，内部自动拼接）
/// @param type MFA 类型，用于选择模板
/// @param templateParams 模板参数
drogon::Task<bool> TencentSMSService::SendSms(const string& phoneNumber, eMFAType type,
                                                const vector<string>& templateParams) {
    // 获取模板ID
    auto it = _templateIds.find(type);
    if (it == _templateIds.end()) {
        LOG_ERROR << "未找到MFA类型对应的短信模板";
        co_return false;
    }

    // 构造请求 body (JSON)
    Json::Value body;
    body["PhoneNumberSet"] = Json::Value(Json::arrayValue);
    body["PhoneNumberSet"].append("+86" + phoneNumber);
    body["SmsSdkAppId"] = _smsSdkAppId;
    body["SignName"] = _signName;
    body["TemplateId"] = it->second;
    body["TemplateParamSet"] = Json::Value(Json::arrayValue);
    for (const auto& p : templateParams)
        body["TemplateParamSet"].append(p);

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    string payload = Json::writeString(wb, body);

    // 计算时间（C++20 chrono，跨平台兼容）
    auto now = chrono::system_clock::now();
    auto timestamp = chrono::duration_cast<chrono::seconds>(now.time_since_epoch()).count();
    auto dp = chrono::floor<chrono::days>(now);
    auto ymd = chrono::year_month_day{dp};
    char dateBuf[16];
    snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d",
             (int)ymd.year(), (unsigned)ymd.month(), (unsigned)ymd.day());
    string date(dateBuf);

    // TC3-HMAC-SHA256 签名
    string service = "sms";
    string canonicalUri = "/";
    string canonicalQuery = "";
    string canonicalHeaders = "content-type:application/json\nhost:sms.tencentcloudapi.com\n";
    string signedHeaders = "content-type;host";
    string hashedPayload = sha256Hex(payload);

    string canonicalRequest = "POST\n" + canonicalUri + "\n" + canonicalQuery + "\n"
                            + canonicalHeaders + "\n" + signedHeaders + "\n" + hashedPayload;

    string stringToSign = "TC3-HMAC-SHA256\n" + to_string(timestamp) + "\n"
                        + date + "/" + service + "/tc3_request\n"
                        + sha256Hex(canonicalRequest);

    string signature = sign(_secretKey, date, service, stringToSign);
    string credentialScope = date + "/" + service + "/tc3_request";
    string authorization = "TC3-HMAC-SHA256 Credential=" + _secretId + "/" + credentialScope
                         + ", SignedHeaders=" + signedHeaders + ", Signature=" + signature;

    // 发送 HTTP 请求
    auto client = drogon::HttpClient::newHttpClient("https://sms.tencentcloudapi.com");
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Post);
    req->setBody(payload);
    req->setContentTypeString("application/json");
    req->addHeader("Host", "sms.tencentcloudapi.com");
    req->addHeader("X-TC-Action", "SendSms");
    req->addHeader("X-TC-Region", _region);
    req->addHeader("X-TC-Timestamp", to_string(timestamp));
    req->addHeader("X-TC-Version", "2021-01-11");
    req->addHeader("Authorization", authorization);

    auto result = co_await client->sendRequestCoro(req);

    if (result->statusCode() != 200) {
        LOG_ERROR << "短信发送失败, HTTP " << result->statusCode()
                  << ": " << string(result->body().data(), result->body().size());
        co_return false;
    }

    // 解析响应
    string respBody(result->body().data(), result->body().size());
    Json::Value resp;
    Json::CharReaderBuilder rb;
    istringstream iss(respBody);
    string errs;
    if (!Json::parseFromStream(rb, iss, &resp, &errs)) {
        LOG_ERROR << "解析短信响应失败: " << errs;
        co_return false;
    }

    auto& sendStatusSet = resp["Response"]["SendStatusSet"];
    if (!sendStatusSet.isNull() && sendStatusSet.size() > 0) {
        auto& status = sendStatusSet[0];
        if (status["Code"].asString() == "Ok") {
            LOG_INFO << "短信发送成功: " << status["SerialNo"].asString();
            co_return true;
        } else {
            LOG_ERROR << "短信发送失败: " << status["Code"].asString()
                      << " - " << status["Message"].asString();
            co_return false;
        }
    }

    LOG_ERROR << "短信发送失败: " << respBody;
    co_return false;
}

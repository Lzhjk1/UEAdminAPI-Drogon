#include "TencentSMSService.h"
#include <drogon/drogon.h>
#include <format>
#include <mutex>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>

#include "utils/MFA/eMFA_Type.h"

#include <trantor/utils/Logger.h>

using namespace std;

namespace {
    std::string sha256hex(const std::string& str) {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256_CTX sha256;
        SHA256_Init(&sha256);
        SHA256_Update(&sha256, str.c_str(), str.size());
        SHA256_Final(hash, &sha256);
        
        std::stringstream ss;
        for(int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        }
        return ss.str();
    }

    std::vector<unsigned char> hmac_sha256(const std::vector<unsigned char>& key, const std::string& data) {
        unsigned int len = SHA256_DIGEST_LENGTH;
        std::vector<unsigned char> hash(len);
        HMAC(EVP_sha256(), key.data(), key.size(),
             reinterpret_cast<const unsigned char*>(data.data()), data.size(),
             hash.data(), &len);
        return hash;
    }

    std::vector<unsigned char> hmac_sha256(const std::string& key, const std::string& data) {
        unsigned int len = SHA256_DIGEST_LENGTH;
        std::vector<unsigned char> hash(len);
        HMAC(EVP_sha256(), key.data(), key.size(),
             reinterpret_cast<const unsigned char*>(data.data()), data.size(),
             hash.data(), &len);
        return hash;
    }

    std::string hex_encode(const std::vector<unsigned char>& data) {
        std::stringstream ss;
        for(auto c : data) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)c;
        }
        return ss.str();
    }
}

TencentSMSService::TencentSMSService(const Json::Value& config) {
    // 读取配置
    _secretId = config["SMS"]["Tencent"]["Secret"]["Id"].asString();
    _secretKey = config["SMS"]["Tencent"]["Secret"]["Key"].asString();
    _region = config["SMS"]["Tencent"]["Region"].asString();
    _smsSdkAppId = config["SMS"]["Tencent"]["SmsSdkApp"]["Id"].asString();
    _signName = config["SMS"]["Tencent"]["SignName"].asString();

    // 读取模板ID
    _templateIds.clear();
    for (const auto& key : config["SMS"]["Tencent"]["TemplateIds"].getMemberNames()) {
        _templateIds[stringToMFAType(key)] = config["SMS"]["Tencent"]["TemplateIds"][key].asString();
    }

    // 检查以上配置是否正确获取
    if (_secretId.empty()){
        LOG_ERROR << "TencentSMS: SecretId 未配置";
        throw std::invalid_argument("TencentSMS: SecretId 未配置");
    }
    if (_secretKey.empty()){
        LOG_ERROR << "TencentSMS: SecretKey 未配置";
        throw std::invalid_argument("TencentSMS: SecretKey 未配置");
    }
    if (_region.empty()){
        LOG_ERROR << "TencentSMS: Region 未配置";
        throw std::invalid_argument("TencentSMS: Region 未配置");
    }
    if (_smsSdkAppId.empty()){
        LOG_ERROR << "TencentSMS: SmsSdkAppId 未配置";
        throw std::invalid_argument("TencentSMS: SmsSdkAppId 未配置");
    }
    if (_signName.empty()){
        LOG_ERROR << "TencentSMS: SignName 未配置";
        throw std::invalid_argument("TencentSMS: SignName 未配置");
    }
    if(_templateIds.empty()){
        LOG_ERROR << "TencentSMS: TemplateIds 未配置";
        throw std::invalid_argument("TencentSMS: TemplateIds 未配置");
    }

    LOG_INFO << "TencentSMSService 初始化完成";
}

Task<bool> TencentSMSService::SendSms(const string &phoneNumber, eMFAType type, const vector<string> &templateParams) {
    string service = "sms";
    string host = "sms.tencentcloudapi.com";
    string action = "SendSms";
    string version = "2021-01-11";
    string endpoint = "https://" + host;
    string method = "POST";

    Json::Value payload;
    
    // Remove '+' from phone number if present, Tencent Cloud requires e.g. "+86138..." -> we need to ensure correct format. 
    // Actually, Tencent Cloud requires "+86138..." format if it's international, or it can accept just the number if it's domestic. 
    // The previous SDK SendSmsRequest also just sets the PhoneNumberSet directly.
    payload["PhoneNumberSet"].append(phoneNumber);
    payload["SmsSdkAppId"] = _smsSdkAppId;
    payload["SignName"] = _signName;
    payload["TemplateId"] = _templateIds[MFATypeToTencentSMSTemplateId(type)];
    
    // Set TemplateParamSet
    if (!templateParams.empty()) {
        for (const auto& param : templateParams) {
            payload["TemplateParamSet"].append(param);
        }
    } else {
        payload["TemplateParamSet"] = Json::arrayValue; // empty array
    }

    Json::FastWriter writer;
    string payloadStr = writer.write(payload);
    // FastWriter adds a newline at the end, which changes the hash. Let's remove it if present.
    if (!payloadStr.empty() && payloadStr.back() == '\n') {
        payloadStr.pop_back();
    }

    // 1. CanonicalRequest
    string canonicalUri = "/";
    string canonicalQueryString = "";
    string canonicalHeaders = "content-type:application/json; charset=utf-8\nhost:" + host + "\n";
    string signedHeaders = "content-type;host";
    string hashedRequestPayload = sha256hex(payloadStr);
    string canonicalRequest = method + "\n" + canonicalUri + "\n" + canonicalQueryString + "\n" + canonicalHeaders + "\n" + signedHeaders + "\n" + hashedRequestPayload;

    // 2. StringToSign
    string algorithm = "TC3-HMAC-SHA256";
    time_t t = time(nullptr);
    string timestamp = to_string(t);

    struct tm tm_info;
#ifdef _WIN32
    gmtime_s(&tm_info, &t);
#else
    gmtime_r(&t, &tm_info);
#endif
    char dateBuf[16];
    strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", &tm_info);
    string date(dateBuf);

    string credentialScope = date + "/" + service + "/tc3_request";
    string hashedCanonicalRequest = sha256hex(canonicalRequest);
    string stringToSign = algorithm + "\n" + timestamp + "\n" + credentialScope + "\n" + hashedCanonicalRequest;

    // 3. Signature
    vector<unsigned char> secretDate = hmac_sha256("TC3" + _secretKey, date);
    vector<unsigned char> secretService = hmac_sha256(secretDate, service);
    vector<unsigned char> secretSigning = hmac_sha256(secretService, "tc3_request");
    string signature = hex_encode(hmac_sha256(secretSigning, stringToSign));

    // 4. Authorization
    string authorization = algorithm + " Credential=" + _secretId + "/" + credentialScope + ", SignedHeaders=" + signedHeaders + ", Signature=" + signature;

    // 5. Send HTTP Request
    auto client = drogon::HttpClient::newHttpClient(endpoint);
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Post);
    req->setPath("/");
    req->addHeader("Host", host);
    req->addHeader("Content-Type", "application/json; charset=utf-8");
    req->addHeader("X-TC-Action", action);
    req->addHeader("X-TC-Version", version);
    req->addHeader("X-TC-Region", _region);
    req->addHeader("X-TC-Timestamp", timestamp);
    req->addHeader("Authorization", authorization);
    req->setBody(payloadStr);

    try {
        auto resp = co_await client->sendRequestCoro(req);
        if (!resp) {
            LOG_ERROR << "短信发送失败: HTTP请求无响应";
            co_return false;
        }

        if (resp->statusCode() != drogon::k200OK) {
            LOG_ERROR << "短信发送失败: HTTP状态码 " << resp->statusCode() << ", body: " << resp->getBody();
            co_return false;
        }

        auto respJson = resp->getJsonObject();
        if (!respJson) {
            LOG_ERROR << "短信发送失败: 无法解析响应JSON: " << resp->getBody();
            co_return false;
        }

        // 检查腾讯云API的业务错误
        if (respJson->isMember("Response") && (*respJson)["Response"].isMember("Error")) {
            LOG_ERROR << "短信发送失败: " << (*respJson)["Response"]["Error"]["Message"].asString();
            co_return false;
        }

        // 检查SendStatusSet，确保第一条发送成功（我们只发了一条）
        if (respJson->isMember("Response") && (*respJson)["Response"].isMember("SendStatusSet")) {
            const auto& statusSet = (*respJson)["Response"]["SendStatusSet"];
            if (statusSet.isArray() && statusSet.size() > 0) {
                const auto& status = statusSet[0];
                if (status["Code"].asString() != "Ok") {
                    LOG_ERROR << "短信发送失败: " << status["Message"].asString() << " (Code: " << status["Code"].asString() << ")";
                    co_return false;
                }
            }
        }

        LOG_INFO << format("已成功向手机号 {} 发送短信", phoneNumber);
        co_return true;

    } catch (const std::exception& e) {
        LOG_ERROR << "短信发送异常: " << e.what();
        co_return false;
    }
}

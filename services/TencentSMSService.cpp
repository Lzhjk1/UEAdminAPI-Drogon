#include "TencentSMSService.h"
#include <drogon/drogon.h>
#include <format>
#include <mutex>

#include <tencentcloud/core/TencentCloud.h>
#include <tencentcloud/core/profile/HttpProfile.h>
#include <tencentcloud/core/profile/ClientProfile.h>
#include <tencentcloud/core/Credential.h>
#include <tencentcloud/core/NetworkProxy.h>
#include <tencentcloud/core/AsyncCallerContext.h>
#include <tencentcloud/sms/v20210111/SmsClient.h>
#include <tencentcloud/sms/v20210111/model/SendSmsRequest.h>
#include <tencentcloud/sms/v20210111/model/SendSmsResponse.h>
#include <trantor/utils/Logger.h>

using namespace TencentCloud;
using namespace TencentCloud::Sms::V20210111;
using namespace TencentCloud::Sms::V20210111::Model;
using namespace std;

bool TencentSMSService::_sInitialized = false;
TencentSMSService* TencentSMSService::_sInstance = nullptr;
static std::mutex _sMutex;

TencentSMSService::TencentSMSService(const Json::Value& config) {
    // 读取配置
    _secretId = config["SMS"]["Tencent"]["Secret"]["Id"].asString();
    _secretKey = config["SMS"]["Tencent"]["Secret"]["Key"].   asString();
    _region = config["SMS"]["Tencent"]["Region"].asString();
    _smsSdkAppId = config["SMS"]["Tencent"]["SmsSdkApp"]["Id"].asString();
    _signName = config["SMS"]["Tencent"]["SignName"].asString();

    // 读取模板ID
    _templateIds.clear();
    for (const auto& key : config["SMS"]["Tencent"]["TemplateIds"].getMemberNames()) {
        _templateIds[stringToMFAType(key)] = config["SMS"]["Tencent"]["TemplateIds"][key].asString();
    }

    // 初始化SDK
    TencentCloud::InitAPI();
    Credential cred = Credential(_secretId, _secretKey);

    _smsClient = std::make_unique<SmsClient>(cred, _region);
}

Task<bool> TencentSMSService::SendSms(const string &phoneNumber, eMFAType type, const vector<string> &templateParams) {
    std::lock_guard<std::mutex> lock(_clientMutex);

    SendSmsRequest req;
    req.SetSmsSdkAppId(_smsSdkAppId);
    req.SetSignName(_signName);
    req.SetTemplateId(_templateIds[type]);
    req.SetPhoneNumberSet({phoneNumber});
    req.SetTemplateParamSet(templateParams);
  
    SmsClient::SendSmsOutcome outcome = co_await SendSmsCoro(req);
    if (!outcome.IsSuccess()) {
        LOG_ERROR << "短信发送失败: " + outcome.GetError().GetErrorMessage();
        co_return false;
    }

    LOG_INFO << format("已成功向手机号 {} 发送短信", req.GetPhoneNumberSet()[0]);
    co_return true;
}

void TencentSMSService::Init(const Json::Value& config) {
    std::lock_guard<std::mutex> lock(_sMutex);
    if(_sInitialized) {
        return;
    }
    _sInitialized = true;
    _sInstance = new TencentSMSService(config);
}

TencentSMSService *TencentSMSService::Instance() {
    if (!_sInitialized) {
        throw std::runtime_error("TencentSMSService not initialized. Call Init() first.");
    }
    return _sInstance;
}

drogon::Task<SmsClient::SendSmsOutcome> TencentSMSService::SendSmsCoro(const SendSmsRequest& req) {
    struct SendSmsAwaiter : public drogon::CallbackAwaiter<SmsClient::SendSmsOutcome> {
        SmsClient* client;
        SendSmsRequest request;

        void await_suspend(std::coroutine_handle<> handle) {
            // 调用异步版本，并在回调中恢复协程
            client->SendSmsAsync(request, [this, handle](const SmsClient *client, const SendSmsRequest& request, SmsClient::SendSmsOutcome outcome, const std::shared_ptr<const AsyncCallerContext>& context) {
                this->setValue(outcome);
                    handle.resume();
                });
        }
    };

    SendSmsAwaiter awaiter;
    awaiter.client = _smsClient.get();

    awaiter.request = req;

    co_return co_await awaiter;
}

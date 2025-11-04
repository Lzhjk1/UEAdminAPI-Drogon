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

    // 初始化SDK
    TencentCloud::InitAPI();
    Credential cred = Credential(_secretId, _secretKey);

    _smsClient = std::make_unique<SmsClient>(cred, _region);
}

Task<bool> TencentSMSService::SendSms(const string &phoneNumber, eMFAType type, const vector<string> &templateParams) {
    SendSmsRequest req;
    {
        std::lock_guard<std::mutex> lock(_clientMutex);
        req.SetSmsSdkAppId(_smsSdkAppId);
        req.SetSignName(_signName);
        req.SetTemplateId(_templateIds[type]);
        req.SetPhoneNumberSet({phoneNumber});
        req.SetTemplateParamSet(templateParams);
    }
    
    // 在锁外执行协程操作，避免跨线程解锁问题
    SmsClient::SendSmsOutcome outcome = co_await SendSmsCoro(req);
    if (!outcome.IsSuccess()) {
        LOG_ERROR << "短信发送失败: " + outcome.GetError().GetErrorMessage();
        co_return false;
    }

    LOG_INFO << format("已成功向手机号 {} 发送短信", req.GetPhoneNumberSet()[0]);
    co_return true;
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

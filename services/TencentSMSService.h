#pragma once

#include <iostream>
#include <map>
#include <unordered_map>
#include <mutex>
#include <utils/MFA/eMFA_Type.h>
#include <json/json.h>
#include <tencentcloud\sms\v20210111\SmsClient.h>
#include <drogon/utils/coroutine.h>
#include "utils/MFA/ISmsService.h"
#include "utils/SingletonWithInit.h"

using namespace std;
using namespace drogon;
using namespace TencentCloud::Sms::V20210111;
using namespace TencentCloud::Sms::V20210111::Model;


class TencentSMSService : public ISmsService, public SingletonWithInit<TencentSMSService> {
private:
    string _secretId, _secretKey, _region, _smsSdkAppId, _signName;
    unordered_map<eMFAType, std::string> _templateIds;

    std::unique_ptr<SmsClient> _smsClient;
    std::mutex _clientMutex; // 保护客户端访问的互斥锁

public:
    TencentSMSService(const Json::Value& config);

    // 腾讯SMS接口的协程包装
    Task<SmsClient::SendSmsOutcome> SendSmsCoro(const SendSmsRequest& req);

    Task<bool> SendSms(const string& phoneNumber, eMFAType type, const vector<string>& templateParams) override;
};


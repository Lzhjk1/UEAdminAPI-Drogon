#pragma once

#include <iostream>
#include <map>
#include <unordered_map>
#include <mutex>
#include <utils/MFA/eMFA_Type.h>
#include <json/json.h>
#include <drogon/utils/coroutine.h>
#include "utils/MFA/ISmsService.h"
#include "utils/SingletonWithInit.h"

using namespace std;
using namespace drogon;

class TencentSMSService : public ISmsService, public SingletonWithInit<TencentSMSService> {
private:
    string _secretId, _secretKey, _region, _smsSdkAppId, _signName;
    unordered_map<eMFAType, std::string> _templateIds;

public:
    TencentSMSService(const Json::Value& config);

    Task<bool> SendSms(const string& phoneNumber, eMFAType type, const vector<string>& templateParams) override;
};


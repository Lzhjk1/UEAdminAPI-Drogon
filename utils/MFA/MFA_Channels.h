#pragma once
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <unordered_map>
#include <vector>
#include "ICodePair.h"
#include "IEmailService.h"
#include "MFACodePair.h"
#include "eMFA_Type.h"
#include "services/TencentSMSService.h"

using namespace std;

enum class eChannelType {
    SMS,
    Email,
    None
};

class IMFAChannel {
public:
    virtual ~IMFAChannel() = default;
    virtual eChannelType ChannelType() const = 0;
    virtual optional<shared_ptr<ICodePair>> GetCodePair(const string &key, const eMFAType& type) = 0;
    virtual drogon::Task<pair<bool, string>> SendCode(shared_ptr<ICodePair> codePair) = 0;
    virtual bool VerifyTheCode(const string &baseInfo, const string &code, eMFAType type, string &errorMsg) = 0;
};

class MFAChannelBase : public IMFAChannel {
protected:
    mutable mutex _mutex;
    vector<shared_ptr<ICodePair>> _listCodePairs;
    eChannelType _channelType;

    void ClearExpired();
    bool AddCodePairToList(shared_ptr<ICodePair> codePair);

public:
    explicit MFAChannelBase(eChannelType channelType);
    eChannelType ChannelType() const override;
    optional<shared_ptr<ICodePair>> GetCodePair(const string &key, const eMFAType& type) override;
    bool VerifyTheCode(const string &baseInfo, const string &code, eMFAType type, string &errorMsg) override;

    static eChannelType DetermineChannelType(const string &target);
};

class MFA_EmailChannel : public MFAChannelBase {
    IEmailService *_emailService;
public:
    MFA_EmailChannel(IEmailService* emailService);
    drogon::Task<pair<bool, string>> SendCode(shared_ptr<ICodePair> codePair) override;
};

class MFA_SMSChannel : public MFAChannelBase {
    ISmsService *_smsService;
public:
    MFA_SMSChannel(ISmsService *smsService);
    drogon::Task<pair<bool, string>> SendCode(shared_ptr<ICodePair> codePair) override;
};
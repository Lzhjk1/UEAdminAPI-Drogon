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
    virtual eChannelType channelType() const = 0;
    virtual optional<shared_ptr<ICodePair>> getCodePair(const string &key) = 0;
    virtual drogon::Task<pair<bool, string>> sendCode(shared_ptr<ICodePair> codePair) = 0;
    virtual bool verifyTheCode(const string &baseInfo, const string &code, eMFAType type, string &errorMsg) = 0;
};

class MFAChannelBase : public IMFAChannel {
protected:
    mutable mutex _mutex;
    vector<shared_ptr<ICodePair>> _listCodePairs;
    eChannelType _channelType;

    void clearExpired();
    bool addCodePairToList(shared_ptr<ICodePair> codePair);

public:
    explicit MFAChannelBase(eChannelType channelType);
    eChannelType channelType() const override;
    optional<shared_ptr<ICodePair>> getCodePair(const string &key) override;
    bool verifyTheCode(const string &baseInfo, const string &code, eMFAType type, string &errorMsg) override;

    static eChannelType determineChannelType(const string &target);
};

class MFA_EmailChannel : public MFAChannelBase {
    IEmailService *_emailService;
public:
    MFA_EmailChannel(IEmailService *emailService);
    drogon::Task<pair<bool, string>> sendCode(shared_ptr<ICodePair> codePair) override;
};

class MFA_SMSChannel : public MFAChannelBase {
    ISmsService *_smsService;
public:
    MFA_SMSChannel(ISmsService *smsService);
    drogon::Task<pair<bool, string>> sendCode(shared_ptr<ICodePair> codePair) override;
};
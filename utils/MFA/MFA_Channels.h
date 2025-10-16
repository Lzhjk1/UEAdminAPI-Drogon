//#pragma once
//#include <drogon/drogon.h>
//#include <mutex>
//#include <vector>
//#include <optional>
//#include <memory>
//#include <unordered_map>
//#include <regex>
//#include "ICodePair.h"
//#include "EmailHelper.h"
//
//enum class eChannelType {
//    SMS,
//    Email,
//    None
//};
//
//class IMFAChannel {
//public:
//    virtual ~IMFAChannel() = default;
//    virtual eChannelType channelType() const = 0;
//    virtual std::optional<std::shared_ptr<ICodePair>> getCodePair(const std::string& key) = 0;
//    virtual drogon::Task<std::pair<bool, std::string>> sendCode(std::shared_ptr<ICodePair> codePair) = 0;
//    virtual bool verifyTheCode(const std::string& baseInfo, const std::string& code, eMFAType type, std::string& errorMsg) = 0;
//};
//
//class MFAChannelBase : public IMFAChannel {
//protected:
//    mutable std::mutex _mutex;
//    std::vector<std::shared_ptr<ICodePair>> _listCodePairs;
//    eChannelType _channelType;
//
//    void clearExpired();
//    bool addCodePairToList(std::shared_ptr<ICodePair> codePair);
//
//public:
//    explicit MFAChannelBase(eChannelType channelType);
//    eChannelType channelType() const override;
//    std::optional<std::shared_ptr<ICodePair>> getCodePair(const std::string& key) override;
//    bool verifyTheCode(const std::string& baseInfo, const std::string& code, eMFAType type, std::string& errorMsg) override;
//
//    static eChannelType determineChannelType(const std::string& target);
//};
//
//class MFA_EmailChannel : public MFAChannelBase {
//    std::shared_ptr<EmailHelper> _emailHelper;
//    std::shared_ptr<drogon::Logger> _logger;
//public:
//    MFA_EmailChannel(std::shared_ptr<drogon::Logger> logger, std::shared_ptr<EmailHelper> emailHelper);
//    drogon::Task<std::pair<bool, std::string>> sendCode(std::shared_ptr<ICodePair> codePair) override;
//};
//
//class MFA_SMSChannel : public MFAChannelBase {
//    std::shared_ptr<drogon::Logger> _logger;
//    std::shared_ptr<drogon::ConfigLoader> _config;
//    std::string _secretId, _secretKey, _region, _smsSdkAppId, _signName;
//    std::unordered_map<eMFAType, std::string> _templateIds;
//public:
//    MFA_SMSChannel(std::shared_ptr<drogon::Logger> logger, std::shared_ptr<drogon::ConfigLoader> config);
//    drogon::Task<std::pair<bool, std::string>> sendCode(std::shared_ptr<ICodePair> codePair) override;
//};
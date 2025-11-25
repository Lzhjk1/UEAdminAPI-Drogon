#include "MFA_Channels.h"

#define LOG_TAG "[MFAChannels]"

// ========== MFAChannelBase ==========

MFAChannelBase::MFAChannelBase(eChannelType channelType)
    : _channelType(channelType) {}

eChannelType MFAChannelBase::ChannelType() const {
    return _channelType;
}

std::optional<std::shared_ptr<ICodePair>> MFAChannelBase::GetCodePair(const std::string &key, const eMFAType& type) {
    std::lock_guard lock(_mutex);
    for (const auto &p : _listCodePairs) {
        if (p->BaseInfo() == key && p->MFAType() == type)
            return p;
    }
    return std::nullopt;
}

bool MFAChannelBase::VerifyTheCode(const std::string &baseInfo, const std::string &code, eMFAType type, std::string &errorMsg) {
    ClearExpired();
    auto target = GetCodePair(baseInfo, type);
    if (!target) {
        errorMsg = "验证码不存在或已过期";
        return false;
    }
    bool result = (target.value()->Code() == code) && ((target.value()->MFAType() & type));
    if (result) {
        std::lock_guard lock(_mutex);
        _listCodePairs.erase(std::remove(_listCodePairs.begin(), _listCodePairs.end(), target.value()), _listCodePairs.end());
    }
    errorMsg = result ? "" : "验证码错误";
    return result;
}

void MFAChannelBase::ClearExpired() {
    std::lock_guard lock(_mutex);
    _listCodePairs.erase(
        std::remove_if(_listCodePairs.begin(), _listCodePairs.end(),
                       [](const std::shared_ptr<ICodePair> &p) { return p->IsExpired(); }),
        _listCodePairs.end());
}

bool MFAChannelBase::AddCodePairToList(std::shared_ptr<ICodePair> codePair) {
    ClearExpired();
    if (!codePair->IsEverythingAllSet())
        return false;
    std::lock_guard lock(_mutex);
    auto it = std::find_if(_listCodePairs.begin(), _listCodePairs.end(),
                           [&](const std::shared_ptr<ICodePair> &p) {
                               return p->BaseInfo() == codePair->BaseInfo() && p->MFAType() == codePair->MFAType();
                           });
    if (it != _listCodePairs.end())
        _listCodePairs.erase(it);
    _listCodePairs.push_back(codePair);
    return true;
}

eChannelType MFAChannelBase::DetermineChannelType(const std::string &target) {
    if (target.empty())
        throw std::invalid_argument("target不能传入空值!");
    static std::regex phoneRegex(R"(^1\d{10}$)");
    static std::regex emailRegex(R"(^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$)");
    if (std::regex_match(target, phoneRegex))
        return eChannelType::SMS;
    if (std::regex_match(target, emailRegex))
        return eChannelType::Email;
    return eChannelType::None;
}

// ========== MFA_EmailChannel ==========

MFA_EmailChannel::MFA_EmailChannel(IEmailService *emailService)
    : MFAChannelBase(eChannelType::Email) 
{
    _emailService = emailService;
}

drogon::Task<std::pair<bool, std::string>> MFA_EmailChannel::SendCode(std::shared_ptr<ICodePair> codePair) {
    if (!codePair) {
        LOG_ERROR << LOG_TAG << ("验证码发送失败, 给出的验证码为空");
        co_return {false, "验证码发送失败, 给出的验证码为空"};
    }
    if (!codePair->IsEverythingAllSet()) {
        LOG_ERROR << LOG_TAG << ("验证码发送失败, 给出的验证码信息不完整");
        co_return {false, "验证码发送失败, 给出的验证码信息不完整"};
    }
    std::string subject = "优易软件验证码";
    std::string content = "您的验证码为: " + codePair->Code();

    std::string ret = co_await _emailService->SendEmailCoro(codePair->BaseInfo(), subject, content);
    // 检查返回的字符串, 如果其中有fail字样则表示发送失败
    // 这种检测方法不稳定, 以后应该会更换emailService
    if (ret.find("EMail sent") == std::string::npos) {
       LOG_ERROR << LOG_TAG << ("验证码发送失败, 邮件发送失败: " + ret);
       co_return {false, "验证码发送失败, 邮件发送失败: " + ret};
    }
    AddCodePairToList(codePair);
    co_return {true, "验证码发送成功, 邮件ID: " + ret};
}

// ========== MFA_SMSChannel ==========

MFA_SMSChannel::MFA_SMSChannel(ISmsService *smsService)
    : MFAChannelBase(eChannelType::SMS) 
{
    _smsService = smsService;
}

drogon::Task<std::pair<bool, std::string>> MFA_SMSChannel::SendCode(std::shared_ptr<ICodePair> codePair) {
    if (!codePair) {
        LOG_ERROR << LOG_TAG << ("短信验证码发送失败, 给出的验证码为空");
        co_return {false, "短信验证码发送失败, 给出的验证码为空"};
    }
    if (!codePair->IsEverythingAllSet()) {
        LOG_ERROR << LOG_TAG << ("短信验证码发送失败, 给出的验证码信息不完整");
        co_return {false, "短信验证码发送失败, 给出的验证码信息不完整"};
    }

    bool isSuccess = co_await _smsService->SendSms(codePair->BaseInfo(), codePair->MFAType(), {codePair->Code()});

    if (!isSuccess) {
        co_return {false, "短信验证码发送失败"};
    }
    AddCodePairToList(codePair);
    co_return {true, "短信验证码发送成功"};
}

bool MFA_SMSChannel::VerifyTheCode(const string &baseInfo, const string &code, eMFAType type, string &errorMsg) {
    auto [countryCode, phoneNumber] = CodePairBase::SMSCodePair::ParsePhoneNumber(baseInfo);
    return MFAChannelBase::VerifyTheCode(countryCode + phoneNumber, code, type, errorMsg);
}

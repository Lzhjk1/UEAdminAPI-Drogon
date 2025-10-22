#include "MFA_Channels.h"

#define LOG_TAG "[MFAChannels]"

// ========== MFAChannelBase ==========

MFAChannelBase::MFAChannelBase(eChannelType channelType)
    : _channelType(channelType) {}

eChannelType MFAChannelBase::channelType() const {
    return _channelType;
}

std::optional<std::shared_ptr<ICodePair>> MFAChannelBase::getCodePair(const std::string &key) {
    std::lock_guard lock(_mutex);
    for (const auto &p : _listCodePairs) {
        if (p->baseInfo() == key)
            return p;
    }
    return std::nullopt;
}

bool MFAChannelBase::verifyTheCode(const std::string &baseInfo, const std::string &code, eMFAType type, std::string &errorMsg) {
    clearExpired();
    auto target = getCodePair(baseInfo);
    if (!target) {
        errorMsg = "验证码不存在或已过期";
        return false;
    }
    bool result = (target.value()->code() == code) && ((target.value()->mfaType() & type));
    if (result) {
        std::lock_guard lock(_mutex);
        _listCodePairs.erase(std::remove(_listCodePairs.begin(), _listCodePairs.end(), target.value()), _listCodePairs.end());
    }
    errorMsg = result ? "" : "验证码错误";
    return result;
}

void MFAChannelBase::clearExpired() {
    std::lock_guard lock(_mutex);
    _listCodePairs.erase(
        std::remove_if(_listCodePairs.begin(), _listCodePairs.end(),
                       [](const std::shared_ptr<ICodePair> &p) { return p->isExpired(); }),
        _listCodePairs.end());
}

bool MFAChannelBase::addCodePairToList(std::shared_ptr<ICodePair> codePair) {
    clearExpired();
    if (!codePair->isEverythingAllSet())
        return false;
    std::lock_guard lock(_mutex);
    auto it = std::find_if(_listCodePairs.begin(), _listCodePairs.end(),
                           [&](const std::shared_ptr<ICodePair> &p) {
                               return p->baseInfo() == codePair->baseInfo() && p->code() == codePair->code();
                           });
    if (it != _listCodePairs.end())
        _listCodePairs.erase(it);
    _listCodePairs.push_back(codePair);
    return true;
}

eChannelType MFAChannelBase::determineChannelType(const std::string &target) {
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

drogon::Task<std::pair<bool, std::string>> MFA_EmailChannel::sendCode(std::shared_ptr<ICodePair> codePair) {
    if (!codePair) {
        LOG_ERROR << LOG_TAG << ("验证码发送失败, 给出的验证码为空");
        co_return {false, "验证码发送失败, 给出的验证码为空"};
    }
    if (!codePair->isEverythingAllSet()) {
        LOG_ERROR << LOG_TAG << ("验证码发送失败, 给出的验证码信息不完整");
        co_return {false, "验证码发送失败, 给出的验证码信息不完整"};
    }
    std::string subject = "优易软件验证码";
    std::string content = "您的验证码为: " + codePair->code();

    //bool sent = co_await _emailHelper->sendEmail(codePair->baseInfo(), subject, content);
    //if (!sent) {
    //    LOG_ERROR << LOG_TAG << ("验证码发送失败, 邮件发送失败");
    //    co_return {false, "验证码发送失败, 邮件发送失败"};
    //}
    addCodePairToList(codePair);
    co_return {true, "验证码发送成功"};
}

// ========== MFA_SMSChannel ==========

MFA_SMSChannel::MFA_SMSChannel(ISmsService *smsService)
    : MFAChannelBase(eChannelType::SMS) 
{
    _smsService = smsService;
}

drogon::Task<std::pair<bool, std::string>> MFA_SMSChannel::sendCode(std::shared_ptr<ICodePair> codePair) {
    if (!codePair) {
        LOG_ERROR << LOG_TAG << ("短信验证码发送失败, 给出的验证码为空");
        co_return {false, "短信验证码发送失败, 给出的验证码为空"};
    }
    if (!codePair->isEverythingAllSet()) {
        LOG_ERROR << LOG_TAG << ("短信验证码发送失败, 给出的验证码信息不完整");
        co_return {false, "短信验证码发送失败, 给出的验证码信息不完整"};
    }

    bool isSuccess = co_await _smsService->SendSms(codePair->baseInfo(), codePair->mfaType(), {codePair->code()});

    if (!isSuccess) {
        co_return {false, "短信验证码发送失败"};
    }
    addCodePairToList(codePair);
    co_return {true, "短信验证码发送成功"};
}
#include "MFAService.h"
#include "utils/MFA/MFACodePair.h"
#include <drogon/drogon.h>
#include <trantor/utils/Logger.h>

MFAService::MFAService(IEmailService* emailService, ISmsService* smsService) {
    _channels.push_back(std::make_shared<MFA_SMSChannel>(smsService));
    _channels.push_back(std::make_shared<MFA_EmailChannel>(emailService));
}

std::shared_ptr<IMFAChannel> MFAService::GetChannel(eChannelType type) {
    std::lock_guard lock(_mutex);
    for (const auto &ch : _channels) {
        if (ch->ChannelType() == type)
            return ch;
    }
    return nullptr;
}

Task<std::tuple<bool, std::string>> MFAService::SendTheCode(const std::string &target, eMFAType type) {
    if (type == eMFAType::Error) {
        co_return std::make_tuple(false, std::string("验证码类型错误"));
    }

    auto channelType = MFAChannelBase::DetermineChannelType(target);
    if (channelType == eChannelType::None) {
        co_return std::make_tuple(false, std::string("无法判断渠道类型"));
    }
    auto channel = GetChannel(channelType);
    if (!channel) {
        co_return std::make_tuple(false, std::string("无法找到对应的验证码发送渠道"));
    }
    std::string innerErrorMsg;
    auto codePair = CodePairBase::CreateCodePair(target, "", type, innerErrorMsg);
    if (!codePair) {
        LOG_ERROR << "验证码发送失败, 生成CodePair失败 : " << innerErrorMsg;
        co_return std::make_tuple(false, innerErrorMsg);
    }
    auto [result, errorMsg] = co_await channel->SendCode(codePair);
    if (!result) {
        LOG_ERROR << "验证码发送失败";
    }
    co_return std::make_tuple(result, errorMsg);
}

Task<std::tuple<bool, std::string>> MFAService::VerifyTheCode(const std::string &target, const std::string &code, eMFAType type, bool isConsume) {
    auto channelType = MFAChannelBase::DetermineChannelType(target);
    if (channelType == eChannelType::None) {
        co_return std::make_tuple(false, std::string("无法判断渠道类型"));
    }
    auto channel = GetChannel(channelType);
    if (!channel) {
        co_return std::make_tuple(false, std::string("无法找到对应的验证码发送渠道"));
    }
    std::string errorMsg;
    bool result = channel->VerifyTheCode(target, code, type, errorMsg, isConsume);
    co_return std::make_tuple(result, errorMsg);
}

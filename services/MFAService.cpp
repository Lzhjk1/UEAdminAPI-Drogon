#include "MFAService.h"
#include "utils/MFA/MFACodePair.h"
#include "utils/TestModeConfig.h"
#include <drogon/drogon.h>
#include <trantor/utils/Logger.h>

MFAService::MFAService(IEmailService* emailService, ISmsService* smsService) {
    _channels.push_back(std::make_shared<MFA_SMSChannel>(smsService));
    _channels.push_back(std::make_shared<MFA_EmailChannel>(emailService));
    
    LOG_INFO << "MFAService 初始化完成";
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

    // 测试模式: 跳过真实发送, 直接入库, 便于测试侧通过调试接口取码
    if (TestModeConfig::Enable()) {
        const auto &fixed = TestModeConfig::FixedCode();
        if (!fixed.empty())
            codePair->SetCode(fixed);
        bool ok = channel->StoreCodePair(codePair);
        if (!ok) {
            LOG_ERROR << "测试模式下验证码入库失败";
            co_return std::make_tuple(false, std::string("验证码入库失败"));
        }
        LOG_INFO << "[TestMode] 验证码已生成(未发送), 目标: "
                 << codePair->BaseInfo() << ", 验证码: " << codePair->Code();
        co_return std::make_tuple(true, std::string("验证码发送成功.(测试模式)"));
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

std::optional<std::string> MFAService::GetLatestCode(const std::string &target, eMFAType type) {
    auto channelType = MFAChannelBase::DetermineChannelType(target);
    if (channelType == eChannelType::None)
        return std::nullopt;
    auto channel = GetChannel(channelType);
    if (!channel)
        return std::nullopt;
    auto codePair = channel->GetCodePair(target, type);
    if (!codePair)
        return std::nullopt;
    if ((*codePair)->IsExpired())
        return std::nullopt;
    return (*codePair)->Code();
}

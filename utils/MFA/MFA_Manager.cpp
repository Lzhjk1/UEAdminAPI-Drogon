//#include "MFA_Manager.h"
//
//MFA_Manager::MFA_Manager(
//    std::shared_ptr<drogon::Logger> logger,
//    std::shared_ptr<drogon::Logger> loggerSMSChannel,
//    std::shared_ptr<drogon::Logger> loggerEmailChannel,
//    std::shared_ptr<drogon::ConfigLoader> configuration,
//    std::shared_ptr<EmailHelper> emailHelper
//) : _logger(std::move(logger)),
//    _configuration(std::move(configuration)),
//    _emailHelper(std::move(emailHelper))
//{
//    // 添加渠道
//    _channels.push_back(std::make_shared<MFA_SMSChannel>(loggerSMSChannel, _configuration));
//    _channels.push_back(std::make_shared<MFA_EmailChannel>(loggerEmailChannel, _emailHelper));
//}
//
//std::shared_ptr<IMFAChannel> MFA_Manager::getChannel(eChannelType type) {
//    std::lock_guard lock(_mutex);
//    for (const auto& ch : _channels) {
//        if (ch->channelType() == type)
//            return ch;
//    }
//    return nullptr;
//}
//
//bool MFA_Manager::verifyTheCode(const std::string& target, const std::string& code, eMFAType type, std::string& errorMsg) {
//    auto channelType = MFAChannelBase::determineChannelType(target);
//    if (channelType == eChannelType::None) {
//        errorMsg = "无法判断渠道类型";
//        return false;
//    }
//    auto channel = getChannel(channelType);
//    if (!channel) {
//        errorMsg = "无法找到对应的验证码发送渠道";
//        return false;
//    }
//    return channel->verifyTheCode(target, code, type, errorMsg);
//}
//
//drogon::Task<std::pair<bool, std::string>> MFA_Manager::sendTheCode(const std::string& target, const std::string& code, eMFAType type) {
//    auto channelType = MFAChannelBase::determineChannelType(target);
//    if (channelType == eChannelType::None) {
//        co_return {false, "无法判断渠道类型"};
//    }
//    auto channel = getChannel(channelType);
//    if (!channel) {
//        co_return {false, "无法找到对应的验证码发送渠道"};
//    }
//    std::string innerErrorMsg;
//    auto codePair = CodePairBase::createCodePair(target, code, type, innerErrorMsg);
//    if (!codePair) {
//        _logger->error("验证码发送失败, 生成CodePair失败 : {}", innerErrorMsg);
//        co_return {false, innerErrorMsg};
//    }
//    auto [result, errorMsg] = co_await channel->sendCode(codePair);
//    if (!result) {
//        _logger->error("验证码发送失败");
//    }
//    co_return {result, errorMsg};
//}
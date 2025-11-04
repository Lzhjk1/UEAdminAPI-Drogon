#include "MFA_Manager.h"

MFA_Manager::MFA_Manager(IEmailService *emailService, ISmsService *smsService)
{
  // 添加渠道
  _channels.push_back(std::make_shared<MFA_SMSChannel>(smsService));
  _channels.push_back(std::make_shared<MFA_EmailChannel>(emailService));
}

std::shared_ptr<IMFAChannel> MFA_Manager::GetChannel(eChannelType type) {
  std::lock_guard lock(_mutex);
  for (const auto& ch : _channels) {
      if (ch->ChannelType() == type)
          return ch;
  }
  return nullptr;
}

std::pair<bool, std::string> MFA_Manager::VerifyTheCode(const std::string& target, const std::string& code, eMFAType type) {
  auto channelType = MFAChannelBase::DetermineChannelType(target);
  if (channelType == eChannelType::None) {
      return {false, "无法判断渠道类型"};
  }
  auto channel = GetChannel(channelType);
  if (!channel) {
      return {false, "无法找到对应的验证码发送渠道"};
  }
  std::string errorMsg;
  bool result = channel->VerifyTheCode(target, code, type, errorMsg);
  return {result, errorMsg};
}

drogon::Task<std::pair<bool, std::string>> MFA_Manager::SendTheCode(const std::string& target, const std::string& code, eMFAType type) {
  auto channelType = MFAChannelBase::DetermineChannelType(target);
  if (channelType == eChannelType::None) {
      co_return {false, "无法判断渠道类型"};
  }
  auto channel = GetChannel(channelType);
  if (!channel) {
      co_return {false, "无法找到对应的验证码发送渠道"};
  }
  std::string innerErrorMsg;
  auto codePair = CodePairBase::CreateCodePair(target, code, type, innerErrorMsg);
  if (!codePair) {
      LOG_ERROR << format("验证码发送失败, 生成CodePair失败 : {}", innerErrorMsg);
      co_return {false, innerErrorMsg};
  }
  auto [result, errorMsg] = co_await channel->SendCode(codePair);
  if (!result) {
      LOG_ERROR << "验证码发送失败";
  }
  co_return {result, errorMsg};
}

#pragma once
#include <drogon/drogon.h>
#include <vector>
#include <memory>
#include <mutex>
#include "MFA_Channels.h"
#include "MFACodePair.h"
#include "utils/Singleton.h"
#include "utils/SingletonWithInit.h"

class MFA_Manager{
  std::mutex _mutex;
  std::vector<std::shared_ptr<IMFAChannel>> _channels;

public:
  MFA_Manager(IEmailService *emailService, ISmsService *smsService);

  std::shared_ptr<IMFAChannel> GetChannel(eChannelType type);

  std::pair<bool, std::string> VerifyTheCode(const std::string& target, const std::string& code, eMFAType type);

  // code留空时自动生成(不可为null)
  drogon::Task<std::pair<bool, std::string>> SendTheCode(const std::string& target, const std::string& code, eMFAType type);
};

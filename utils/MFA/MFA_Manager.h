#pragma once
#include <drogon/drogon.h>
#include <vector>
#include <memory>
#include <mutex>
#include "MFA_Channels.h"
#include "MFACodePair.h"

class MFA_Manager {
  std::mutex _mutex;
  std::vector<std::shared_ptr<IMFAChannel>> _channels;

public:
  MFA_Manager(IEmailService *emailService, ISmsService *smsService);

  std::shared_ptr<IMFAChannel> getChannel(eChannelType type);

  bool verifyTheCode(const std::string& target, const std::string& code, eMFAType type, std::string& errorMsg);

  drogon::Task<std::pair<bool, std::string>> sendTheCode(const std::string& target, const std::string& code, eMFAType type);
};
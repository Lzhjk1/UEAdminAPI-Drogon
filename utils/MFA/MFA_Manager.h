//#pragma once
//#include <drogon/drogon.h>
//#include <vector>
//#include <memory>
//#include <mutex>
//#include "MFA_Channels.h"
//#include "MFACodePair.h"
//#include "EmailHelper.h"
//
//class MFA_Manager {
//    std::shared_ptr<drogon::Logger> _logger;
//    std::shared_ptr<drogon::ConfigLoader> _configuration;
//    std::shared_ptr<EmailHelper> _emailHelper;
//    std::mutex _mutex;
//    std::vector<std::shared_ptr<IMFAChannel>> _channels;
//
//public:
//    MFA_Manager(
//        std::shared_ptr<drogon::Logger> logger,
//        std::shared_ptr<drogon::Logger> loggerSMSChannel,
//        std::shared_ptr<drogon::Logger> loggerEmailChannel,
//        std::shared_ptr<drogon::ConfigLoader> configuration,
//        std::shared_ptr<EmailHelper> emailHelper
//    );
//
//    std::shared_ptr<IMFAChannel> getChannel(eChannelType type);
//
//    bool verifyTheCode(const std::string& target, const std::string& code, eMFAType type, std::string& errorMsg);
//
//    drogon::Task<std::pair<bool, std::string>> sendTheCode(const std::string& target, const std::string& code, eMFAType type);
//};
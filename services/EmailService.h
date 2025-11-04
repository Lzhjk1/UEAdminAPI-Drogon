#pragma once

#include <iostream>
#include "utils/SingletonWithInit.h"
#include "plugins/SMTPMail.h"
#include <drogon/utils/coroutine.h>
#include "utils/MFA/IEmailService.h"

// 目前用的邮件插件是无状态的, 高频使用可能会触发频繁登录的异常
class EmailService : public IEmailService, public SingletonWithInit<EmailService> {
private:
    IEmailSender* _emailSender;

    std::string _smtpServer;
    uint16_t _smtpPort;
    std::string _smtpUser;
    std::string _smtpPassword;
    std::string _claimedName;


public:
    EmailService(IEmailSender* emailPlugin, const Json::Value& config);

    drogon::Task<std::string> SendEmailCoro(
        const std::string& to, 
        const std::string& subject, 
        const std::string& content) override;
};
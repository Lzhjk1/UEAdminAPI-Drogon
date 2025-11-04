#pragma once
#include <iostream>
#include <drogon/utils/coroutine.h>


class IEmailSender {
public:
    virtual ~IEmailSender() = default;
    virtual drogon::Task<std::string> sendEmailCoro(
        const std::string &mailServer,
        const uint16_t &port,
        const std::string &from,
        const std::string &to,
        const std::string &subject,
        const std::string &content,
        const std::string &user,
        const std::string &passwd,
        bool isHTML) = 0;
};

class IEmailService {
public:
    virtual ~IEmailService() = default;
    virtual drogon::Task<std::string> SendEmailCoro(
        const std::string& to,
        const std::string& subject,
        const std::string& content) = 0;
};
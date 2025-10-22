#pragma once
#include <iostream>
#include <drogon/utils/coroutine.h>


class IEmailService {
public:
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
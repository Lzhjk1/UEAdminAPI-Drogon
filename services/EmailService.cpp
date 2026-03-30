#include "EmailService.h"

EmailService::EmailService(IEmailSender *emailSender, const Json::Value& config) {
    _emailSender = emailSender;
    
    // 读取配置
    _smtpServer = config["Email"]["SmtpServer"].asString();
    _smtpPort = config["Email"]["SmtpPort"].asUInt();
    _smtpUser = config["Email"]["EmailSource"].asString();
    _smtpPassword = config["Email"]["EmailPassword"].asString();
    _claimedName = config["Email"]["ClaimedName"].asString();
    
    // 检查配置是否正确获取
    if (_smtpServer.empty()){
        LOG_ERROR << "EmailService: SMTP服务器未配置";
        throw std::invalid_argument("EmailService: SMTP服务器未配置");
    }
    if (_smtpPort == 0){
        LOG_ERROR << "EmailService: SMTP端口未配置";
        throw std::invalid_argument("EmailService: SMTP端口未配置");
    }
    if (_smtpUser.empty()){
        LOG_ERROR << "EmailService: SMTP用户名未配置";
        throw std::invalid_argument("EmailService: SMTP用户名未配置");
    }
    if (_smtpPassword.empty()){
        LOG_ERROR << "EmailService: SMTP密码未配置";
        throw std::invalid_argument("EmailService: SMTP密码未配置");
    }
    if (_claimedName.empty()){
        LOG_ERROR << "EmailService: 发件人名称未配置";
        throw std::invalid_argument("EmailService: 发件人名称未配置");
    }

    LOG_INFO << "EmailService 初始化完成";
}

drogon::Task<std::string> EmailService::SendEmailCoro(const std::string &to, const std::string &subject, const std::string &content) {
    co_return co_await _emailSender->sendEmailCoro(_smtpServer, _smtpPort, _smtpUser, to, subject, content, _smtpUser, _smtpPassword, false);
}

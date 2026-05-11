#include "EMailHelper.h"
#include "plugins/SMTPMail.h"

UEAdminAPI::EmailHelper::EmailHelper() {
	auto& jsonConfig = drogon::app().getCustomConfig();

	_emailSource = jsonConfig["emailSource"].asString();
	_emailPassword = jsonConfig["emailPassword"].asString();
	_claimedName = jsonConfig["claimedName"].asString();

	_smtpServer = jsonConfig["smtpServer"].asString();
	_smtpPort = jsonConfig["smtpPort"].asInt();

	bool isSomethingNoSet = false;
	// 逐个检查有没有空的
	if (_emailSource.empty()) {
		LOG_ERROR << "emailSource 未配置";
		isSomethingNoSet = true;
	}
	if (_emailPassword.empty()) {
		LOG_ERROR << "emailPassword 未配置";
		isSomethingNoSet = true;
	}
	if (_claimedName.empty()) {
		LOG_ERROR << "claimedName 未配置";
		isSomethingNoSet = true;
	}
	if (_smtpServer.empty()) {
		LOG_ERROR << "smtpServer 未配置";
        isSomethingNoSet = true;
	}
	if (_smtpPort == 0) {
		LOG_ERROR << "smtpPort 未配置";
        isSomethingNoSet = true;
	}

    if(isSomethingNoSet){
        LOG_ERROR << "EmailHelper 初始化失败, 请检查配置项";
        throw std::runtime_error("EmailHelper 初始化失败, 请检查配置项");
    }

}

Task<bool> UEAdminAPI::EmailHelper::SendEmailCoro(const std::string& targetMailbox, const std::string& subject, const std::string& content) {
    throw std::runtime_error("EmailHelper::SendEmail 未实现");
}

bool UEAdminAPI::EmailHelper::SendEmail(const std::string &targetMailbox, const std::string &subject, const std::string &content) {
    auto smtpPlugin = drogon::app().getPlugin<SMTPMail>();
    if (!smtpPlugin) {
        LOG_ERROR << "SMTPMail 插件未加载, 无法发送邮件";
        return false;
    }

    // emailId 目前没有使用
    std::string emailId = smtpPlugin->sendEmail(
        _smtpServer,        // SMTP 服务器地址
        _smtpPort,          // SMTP 服务器端口
        _emailSource,       // 发件人邮箱
        targetMailbox,      // 收件人邮箱
        subject,            // 邮件主题
        content,            // 邮件内容
        _emailSource,       // SMTP 用户名 (通常与发件人邮箱相同)
        _emailPassword,     // SMTP 密码
        true,            // 是否为 HTML 格式
        // 完成后的回调, 使用匿名函数
        [](const std::string &result) {
            LOG_DEBUG << "邮件发送结果: " << result;
        }
    );

    return true;

}

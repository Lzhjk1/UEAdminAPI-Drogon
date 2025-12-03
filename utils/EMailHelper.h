#pragma once
#include <iostream>
#include <string>
#include <drogon/drogon.h>
#include <drogon/utils/coroutine.h>


using namespace drogon;

namespace UEAdminAPI {
	class EmailHelper {
	private:
		std::string _emailSource;
		std::string _emailPassword;
		std::string _claimedName;

		std::string _smtpServer;
		int _smtpPort;

	public:
		EmailHelper();

		Task<bool> SendEmailCoro(const std::string& targetMailbox, const std::string& subject, const std::string& content);
        bool SendEmail(const std::string& targetMailbox, const std::string& subject, const std::string& content);
	};
}
#pragma once
#include <iostream>
#include <string>
#include <drogon/drogon.h>
#include <drogon/utils/coroutine.h>


using namespace drogon;
using namespace std;

namespace UEAdminAPI {
	class EmailHelper {
	private:
		string _emailSource;
		string _emailPassword;
		string _claimedName;

		string _smtpServer;
		int _smtpPort;

	public:
		EmailHelper();

		Task<bool> SendEmailCoro(const std::string& targetMailbox, const std::string& subject, const std::string& content);
        bool SendEmail(const std::string& targetMailbox, const std::string& subject, const std::string& content);
	};
}
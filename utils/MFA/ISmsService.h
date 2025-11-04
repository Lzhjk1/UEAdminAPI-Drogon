#pragma once

#include <iostream>
#include <drogon/drogon.h>
#include "eMFA_Type.h"

class ISmsService {
public:
	virtual ~ISmsService() = default;
	virtual drogon::Task<bool> SendSms(const std::string& phoneNumber, eMFAType type, const std::vector<std::string>& templateParams) = 0;
};
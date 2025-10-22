#pragma once

#include <iostream>
#include <chrono>

#include "eMFA_Type.h"

// 接口
class ICodePair {
public:
	virtual ~ICodePair() = default;
	virtual const std::string& code() const = 0;
	virtual void setCode(const std::string&) = 0;
	virtual eMFAType mfaType() const = 0;
	virtual void setMfaType(eMFAType) = 0;
	virtual std::chrono::system_clock::time_point expireTime() const = 0;
	virtual void setExpireTime(std::chrono::system_clock::time_point) = 0;
	virtual bool isExpired() const = 0;
	virtual bool isEverythingAllSet() const = 0;
	virtual std::string baseInfo() const = 0;
};
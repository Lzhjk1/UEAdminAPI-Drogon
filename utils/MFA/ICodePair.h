#pragma once

#include <iostream>
#include <chrono>

#include "eMFA_Type.h"

// 接口
class ICodePair {
public:
	virtual ~ICodePair() = default;
	virtual const std::string& Code() const = 0;
	virtual void SetCode(const std::string&) = 0;
	virtual eMFAType MFAType() const = 0;
	virtual void SetMfaType(eMFAType) = 0;
	virtual std::chrono::system_clock::time_point ExpireTime() const = 0;
	virtual void SetExpireTime(std::chrono::system_clock::time_point) = 0;
	virtual bool IsExpired() const = 0;
	virtual bool IsEverythingAllSet() const = 0;
	virtual std::string BaseInfo() const = 0;
	// 记录一次校验失败；达到上限返回 true，调用方应作废该验证码。
	virtual bool RecordFailure(int maxFailures) = 0;
	virtual int FailureCount() const = 0;
};
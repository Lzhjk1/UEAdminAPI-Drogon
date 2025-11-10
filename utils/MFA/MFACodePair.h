#pragma once
#include <string>
#include <chrono>
#include <memory>
#include <optional>
#include <regex>
#include "MFA_Channels.h"
#include "eMFA_Type.h"
#include "ICodePair.h"

// DTO
struct MFAReceiveDto {
   std::optional<eMFAType> Type;
   std::optional<std::string> PhoneNumber;
};



// 抽象基类
class CodePairBase : public ICodePair {
protected:
   std::string _code;
   eMFAType _mfaType = eMFAType::Default;
   std::chrono::system_clock::time_point _expireTime;

public:
   CodePairBase();
   const std::string& Code() const override;
   void SetCode(const std::string&) override;
   eMFAType MFAType() const override;
   void SetMfaType(eMFAType) override;
   std::chrono::system_clock::time_point ExpireTime() const override;
   void SetExpireTime(std::chrono::system_clock::time_point) override;
   bool IsExpired() const override;
   virtual bool IsEverythingAllSet() const override;
   virtual std::string BaseInfo() const override = 0;

   // code留空时自动生成
   static std::shared_ptr<ICodePair> CreateCodePair(const std::string& target, const std::string& code, eMFAType type, std::string& errorMsg);

   // 短信验证码
   class SMSCodePair;
   // 邮箱验证码
   class EmailCodePair;
};

// 短信验证码
class CodePairBase::SMSCodePair : public CodePairBase {
   std::string _phoneNumber;
   std::string _countryCode;
public:
   SMSCodePair(const std::string& phoneNumber, const std::string& code, eMFAType mfaType, std::optional<std::chrono::system_clock::time_point> expireTime = std::nullopt);
   SMSCodePair(const std::string& phoneNumber, eMFAType mfaType, std::optional<std::chrono::system_clock::time_point> expireTime = std::nullopt);
   
   // 工具函数：解析电话号码，返回(countryCode, phoneNumber)元组
   static std::tuple<std::string, std::string> ParsePhoneNumber(const std::string& phoneNumber);
   std::string PhoneNumber() const;
   void SetPhoneNumber(const std::string&);
   std::string CountryCode() const;
   void SetCountryCode(const std::string&);
   bool IsEverythingAllSet() const override;
   std::string BaseInfo() const override;
};

// 邮箱验证码
class CodePairBase::EmailCodePair : public CodePairBase {
   std::string _email;
public:
   EmailCodePair(const std::string& email, const std::string& code, eMFAType mfaType, std::optional<std::chrono::system_clock::time_point> expireTime = std::nullopt);
   std::string Email() const;
   void SetEmail(const std::string&);
   bool IsEverythingAllSet() const override;
   std::string BaseInfo() const override;
};
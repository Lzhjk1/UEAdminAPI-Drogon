#include "MFACodePair.h"
#include <random>
#include <stdexcept>
#include <ctime>

// 工具函数：生成6位数字验证码
static std::string generate6DigitCode() {
   static thread_local std::mt19937 gen(std::random_device{}());
   std::uniform_int_distribution<int> dist(0, 9);
   std::string code;
   for (int i = 0; i < 6; ++i) code += '0' + dist(gen);
   return code;
}

// ========== CodePairBase ==========

CodePairBase::CodePairBase() {
   _expireTime = std::chrono::system_clock::now() + std::chrono::minutes(5);
}

const std::string& CodePairBase::code() const {
   if (_code.empty()) {
       const_cast<CodePairBase*>(this)->_code = generate6DigitCode();
   }
   return _code;
}
void CodePairBase::setCode(const std::string& value) {
   if (value.empty()) throw std::runtime_error("传入的值为空!");
   if (!std::regex_match(value, std::regex(R"(\d{6})")))
       throw std::invalid_argument("验证码限定为6位纯数字!");
   _code = value;
}
eMFAType CodePairBase::mfaType() const { return _mfaType; }
void CodePairBase::setMfaType(eMFAType t) { _mfaType = t; }
std::chrono::system_clock::time_point CodePairBase::expireTime() const { return _expireTime; }
void CodePairBase::setExpireTime(std::chrono::system_clock::time_point t) { _expireTime = t; }
bool CodePairBase::isExpired() const {
   return std::chrono::system_clock::now() > _expireTime;
}
bool CodePairBase::isEverythingAllSet() const {
   return !code().empty() && _mfaType != eMFAType::Default && !isExpired();
}

// 工厂方法
std::shared_ptr<ICodePair> CodePairBase::createCodePair(const std::string& target, const std::string& code, eMFAType type, std::string& errorMsg) {
   auto channel = MFAChannelBase::determineChannelType(target);
   if (channel == eChannelType::None) {
       errorMsg = "无法识别目标类型";
       return nullptr;
   }
   errorMsg.clear();
   if (channel == eChannelType::SMS)
       return std::make_shared<SMSCodePair>(target, code, type);
   if (channel == eChannelType::Email)
       return std::make_shared<EmailCodePair>(target, code, type);
   errorMsg = "无法识别目标类型";
   return nullptr;
}

// ========== SMSCodePair ==========

CodePairBase::SMSCodePair::SMSCodePair(const std::string& phoneNumber, const std::string& code, eMFAType mfaType, const std::string& countryCode, std::optional<std::chrono::system_clock::time_point> expireTime) {
   _phoneNumber = phoneNumber;
   setCode(code);
   setMfaType(mfaType);
   _countryCode = countryCode;
   _expireTime = expireTime.value_or(std::chrono::system_clock::now() + std::chrono::minutes(5));
}
CodePairBase::SMSCodePair::SMSCodePair(const std::string& phoneNumber, eMFAType mfaType, const std::string& countryCode, std::optional<std::chrono::system_clock::time_point> expireTime) {
   _phoneNumber = phoneNumber;
   setMfaType(mfaType);
   _countryCode = countryCode;
   _expireTime = expireTime.value_or(std::chrono::system_clock::now() + std::chrono::minutes(5));
}
std::string CodePairBase::SMSCodePair::phoneNumber() const { return _countryCode + _phoneNumber; }
void CodePairBase::SMSCodePair::setPhoneNumber(const std::string& v) { _phoneNumber = v; }
std::string CodePairBase::SMSCodePair::countryCode() const { return _countryCode; }
void CodePairBase::SMSCodePair::setCountryCode(const std::string& v) { _countryCode = v; }
bool CodePairBase::SMSCodePair::isEverythingAllSet() const {
   return CodePairBase::isEverythingAllSet() && !_countryCode.empty() && !_phoneNumber.empty();
}
std::string CodePairBase::SMSCodePair::baseInfo() const { return phoneNumber(); }

// ========== EmailCodePair ==========

CodePairBase::EmailCodePair::EmailCodePair(const std::string& email, const std::string& code, eMFAType mfaType, std::optional<std::chrono::system_clock::time_point> expireTime) {
   setEmail(email);
   setCode(code);
   setMfaType(mfaType);
   _expireTime = expireTime.value_or(std::chrono::system_clock::now() + std::chrono::minutes(5));
}
std::string CodePairBase::EmailCodePair::email() const { return _email; }
void CodePairBase::EmailCodePair::setEmail(const std::string& v) {
   if (v.empty()) throw std::runtime_error("传入的值为空!");
   static std::regex emailRegex(R"(^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$)");
   if (!std::regex_match(v, emailRegex))
       throw std::invalid_argument("邮箱格式不正确!");
   _email = v;
}
bool CodePairBase::EmailCodePair::isEverythingAllSet() const {
   return CodePairBase::isEverythingAllSet() && !_email.empty();
}
std::string CodePairBase::EmailCodePair::baseInfo() const { return email(); }
#include "MFACodePair.h"
#include <random>
#include <stdexcept>
#include <ctime>
#include <locale>
#include <codecvt>

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

const std::string& CodePairBase::Code() const {
   if (_code.empty()) {
       const_cast<CodePairBase*>(this)->_code = generate6DigitCode();
   }
   return _code;
}

void CodePairBase::SetCode(const std::string& value) {
   if (value.empty()) {
        _code = generate6DigitCode();
        return;
   }
    if (!std::regex_match(value, std::regex(R"(\d{6})")))
        throw std::invalid_argument("code must be 6 digits");
    _code = value;
}
eMFAType CodePairBase::MFAType() const { return _mfaType; }
void CodePairBase::SetMfaType(eMFAType t) { _mfaType = t; }
std::chrono::system_clock::time_point CodePairBase::ExpireTime() const { return _expireTime; }
void CodePairBase::SetExpireTime(std::chrono::system_clock::time_point t) { _expireTime = t; }
bool CodePairBase::IsExpired() const {
   return std::chrono::system_clock::now() > _expireTime;
}
bool CodePairBase::IsEverythingAllSet() const {
   return !Code().empty() && _mfaType != eMFAType::Error && !IsExpired();
}

// 工厂方法
std::shared_ptr<ICodePair> CodePairBase::CreateCodePair(const std::string& target, const std::string& code, eMFAType type, std::string& errorMsg) {
   auto channel = MFAChannelBase::DetermineChannelType(target);
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

// 工具函数：解析电话号码，返回(countryCode, phoneNumber)元组
std::tuple<std::string, std::string> CodePairBase::SMSCodePair::ParsePhoneNumber(const std::string& phoneNumber) {
   // 检查号码是否带区号
   if (phoneNumber.find("+") == 0) {
      // 找到区号和号码的分界点
      size_t endOfCountryCode = phoneNumber.find(" ", 1);
      if (endOfCountryCode == std::string::npos) {
         // 如果没有空格，尝试找到数字和非数字的分界点
         endOfCountryCode = 1;
         while (endOfCountryCode < phoneNumber.length() && isdigit(phoneNumber[endOfCountryCode])) {
            endOfCountryCode++;
         }
      }
      
      // 提取区号
      std::string countryCode = phoneNumber.substr(0, endOfCountryCode);
      
      // 提取纯号码（跳过可能的空格）
      size_t startOfPhoneNumber = endOfCountryCode;
      if (startOfPhoneNumber < phoneNumber.length() && phoneNumber[startOfPhoneNumber] == ' ') {
         startOfPhoneNumber++;
      }
      std::string purePhoneNumber = phoneNumber.substr(startOfPhoneNumber);
      
      return std::make_tuple(countryCode, purePhoneNumber);
   } else {
      // 没有区号，默认+86
      return std::make_tuple("+86", phoneNumber);
   }
}

CodePairBase::SMSCodePair::SMSCodePair(const std::string& phoneNumber, const std::string& code, eMFAType mfaType, std::optional<std::chrono::system_clock::time_point> expireTime) {
   // 使用ParsePhoneNumber解析电话号码
   auto [countryCode, purePhoneNumber] = ParsePhoneNumber(phoneNumber);
   _countryCode = countryCode;
   _phoneNumber = purePhoneNumber;
   
   SetCode(code);
   SetMfaType(mfaType);
   _expireTime = expireTime.value_or(std::chrono::system_clock::now() + std::chrono::minutes(5));
}
CodePairBase::SMSCodePair::SMSCodePair(const std::string& phoneNumber, eMFAType mfaType, std::optional<std::chrono::system_clock::time_point> expireTime) {
   // 使用ParsePhoneNumber解析电话号码
   auto [countryCode, purePhoneNumber] = ParsePhoneNumber(phoneNumber);
   _countryCode = countryCode;
   _phoneNumber = purePhoneNumber;
   
   SetMfaType(mfaType);
   _expireTime = expireTime.value_or(std::chrono::system_clock::now() + std::chrono::minutes(5));
}
std::string CodePairBase::SMSCodePair::PhoneNumber() const { return _countryCode + _phoneNumber; }
void CodePairBase::SMSCodePair::SetPhoneNumber(const std::string& v) { _phoneNumber = v; }
std::string CodePairBase::SMSCodePair::CountryCode() const { return _countryCode; }
void CodePairBase::SMSCodePair::SetCountryCode(const std::string& v) { _countryCode = v; }
bool CodePairBase::SMSCodePair::IsEverythingAllSet() const {
   return CodePairBase::IsEverythingAllSet() && !_countryCode.empty() && !_phoneNumber.empty();
}
std::string CodePairBase::SMSCodePair::BaseInfo() const { return PhoneNumber(); }

// ========== EmailCodePair ==========

CodePairBase::EmailCodePair::EmailCodePair(const std::string& email, const std::string& code, eMFAType mfaType, std::optional<std::chrono::system_clock::time_point> expireTime) {
   SetEmail(email);
   SetCode(code);
   SetMfaType(mfaType);
   _expireTime = expireTime.value_or(std::chrono::system_clock::now() + std::chrono::minutes(5));
}
std::string CodePairBase::EmailCodePair::Email() const { return _email; }
void CodePairBase::EmailCodePair::SetEmail(const std::string& v) {
   if (v.empty()) throw std::runtime_error("传入的值为空!");
   static std::regex emailRegex(R"(^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$)");
   if (!std::regex_match(v, emailRegex))
       throw std::invalid_argument("邮箱格式不正确!");
   _email = v;
}
bool CodePairBase::EmailCodePair::IsEverythingAllSet() const {
   return CodePairBase::IsEverythingAllSet() && !_email.empty();
}
std::string CodePairBase::EmailCodePair::BaseInfo() const { return Email(); }
//#pragma once
//#include <string>
//#include <chrono>
//#include <memory>
//#include <optional>
//#include <regex>
//#include "MFA_Channels.h"
//
//// 枚举类型
//enum class eMFAType : uint32_t {
//    Default        = 1 << 0,
//    Login          = 1 << 1,
//    Register       = 1 << 2,
//    ResetPassword  = 1 << 3,
//    EmailBind      = 1 << 4,
//    PhoneChange    = 1 << 5,
//    DeleteUser     = 1 << 6,
//    LoginOrRegister = Login | Register
//};
//
//inline eMFAType operator|(eMFAType a, eMFAType b) {
//    return static_cast<eMFAType>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
//}
//inline bool operator&(eMFAType a, eMFAType b) {
//    return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
//}
//
//// DTO
//struct MFAReceiveDto {
//    std::optional<eMFAType> type;
//    std::optional<std::string> phoneNumber;
//};
//
//// 接口
//class ICodePair {
//public:
//    virtual ~ICodePair() = default;
//    virtual const std::string& code() const = 0;
//    virtual void setCode(const std::string&) = 0;
//    virtual eMFAType mfaType() const = 0;
//    virtual void setMfaType(eMFAType) = 0;
//    virtual std::chrono::system_clock::time_point expireTime() const = 0;
//    virtual void setExpireTime(std::chrono::system_clock::time_point) = 0;
//    virtual bool isExpired() const = 0;
//    virtual bool isEverythingAllSet() const = 0;
//    virtual std::string baseInfo() const = 0;
//};
//
//// 抽象基类
//class CodePairBase : public ICodePair {
//protected:
//    std::string _code;
//    eMFAType _mfaType = eMFAType::Default;
//    std::chrono::system_clock::time_point _expireTime;
//
//public:
//    CodePairBase();
//    const std::string& code() const override;
//    void setCode(const std::string&) override;
//    eMFAType mfaType() const override;
//    void setMfaType(eMFAType) override;
//    std::chrono::system_clock::time_point expireTime() const override;
//    void setExpireTime(std::chrono::system_clock::time_point) override;
//    bool isExpired() const override;
//    virtual bool isEverythingAllSet() const override;
//    virtual std::string baseInfo() const override = 0;
//
//    static std::shared_ptr<ICodePair> createCodePair(const std::string& target, const std::string& code, eMFAType type, std::string& errorMsg);
//
//    // 短信验证码
//    class SMSCodePair;
//    // 邮箱验证码
//    class EmailCodePair;
//};
//
//// 短信验证码
//class CodePairBase::SMSCodePair : public CodePairBase {
//    std::string _phoneNumber;
//    std::string _countryCode;
//public:
//    SMSCodePair(const std::string& phoneNumber, const std::string& code, eMFAType mfaType, const std::string& countryCode = "+86", std::optional<std::chrono::system_clock::time_point> expireTime = std::nullopt);
//    SMSCodePair(const std::string& phoneNumber, eMFAType mfaType, const std::string& countryCode = "+86", std::optional<std::chrono::system_clock::time_point> expireTime = std::nullopt);
//    std::string phoneNumber() const;
//    void setPhoneNumber(const std::string&);
//    std::string countryCode() const;
//    void setCountryCode(const std::string&);
//    bool isEverythingAllSet() const override;
//    std::string baseInfo() const override;
//};
//
//// 邮箱验证码
//class CodePairBase::EmailCodePair : public CodePairBase {
//    std::string _email;
//public:
//    EmailCodePair(const std::string& email, const std::string& code, eMFAType mfaType, std::optional<std::chrono::system_clock::time_point> expireTime = std::nullopt);
//    std::string email() const;
//    void setEmail(const std::string&);
//    bool isEverythingAllSet() const override;
//    std::string baseInfo() const override;
//};
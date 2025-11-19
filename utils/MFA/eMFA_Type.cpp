#include "eMFA_Type.h"
#include <stdexcept>
#include <trantor/utils/Logger.h>

eMFAType stringToMFAType(const std::string& str) {
    if (str == "Default") return eMFAType::Default;
    else if (str == "Login") return eMFAType::Login;
    else if (str == "Register") return eMFAType::Register;
    else if (str == "ResetPassword") return eMFAType::ResetPassword;
    else if (str == "EmailBind") return eMFAType::EmailBind;
    else if (str == "ThirdPartyBind") return eMFAType::ThirdPartyBind;
    else if (str == "PhoneChange") return eMFAType::PhoneChange;
    else if (str == "DeleteUser") return eMFAType::DeleteUser;
    
    LOG_ERROR << "发现未知的MFA类型: " + str;
    return eMFAType::Error;
}

std::string MFATypeToString(eMFAType type) {
    switch (type) {
        case eMFAType::Error: return "Error";
        case eMFAType::Default: return "Default";
        case eMFAType::Login: return "Login";
        case eMFAType::Register: return "Register";
        case eMFAType::ResetPassword: return "ResetPassword";
        case eMFAType::EmailBind: return "EmailBind";
        case eMFAType::ThirdPartyBind: return "ThirdPartyBind";
        case eMFAType::PhoneChange: return "PhoneChange";
        case eMFAType::DeleteUser: return "DeleteUser";
        default: throw std::invalid_argument("Unknown MFA type");
    }
}

eMFAType operator|(eMFAType a, eMFAType b) {
    if (a == eMFAType::Error || b == eMFAType::Error){
        throw std::invalid_argument("Cannot combine Error MFA type");
    }
    return static_cast<eMFAType>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

bool operator&(eMFAType a, eMFAType b) {
    if (a == eMFAType::Error || b == eMFAType::Error){
        throw std::invalid_argument("Cannot combine Error MFA type");
    }
    return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}
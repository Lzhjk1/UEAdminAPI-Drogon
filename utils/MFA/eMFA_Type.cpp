#include "eMFA_Type.h"
#include <stdexcept>

eMFAType stringToMFAType(const std::string& str) {
    if (str == "Default") return eMFAType::Default;
    else if (str == "Login") return eMFAType::Login;
    else if (str == "Register") return eMFAType::Register;
    else if (str == "ResetPassword") return eMFAType::ResetPassword;
    else if (str == "EmailBind") return eMFAType::EmailBind;
    else if (str == "PhoneChange") return eMFAType::PhoneChange;
    else if (str == "DeleteUser") return eMFAType::DeleteUser;
    else if (str == "LoginOrRegister") return eMFAType::LoginOrRegister;
    throw std::invalid_argument("Unknown MFA type: " + str);
}

eMFAType operator|(eMFAType a, eMFAType b) {
    return static_cast<eMFAType>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

bool operator&(eMFAType a, eMFAType b) {
    return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}
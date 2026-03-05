#include "eMFA_Type.h"
#include <stdexcept>
#include <trantor/utils/Logger.h>
#include <algorithm>
#include <cctype>

eMFAType stringToMFAType(const std::string& str) {
    std::string lowerStr = str;
    std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    if (lowerStr == "default") return eMFAType::Default;
    else if (lowerStr == "login") return eMFAType::Login;
    else if (lowerStr == "register") return eMFAType::Register;
    else if (lowerStr == "resetpassword") return eMFAType::ResetPassword;
    else if (lowerStr == "emailbind") return eMFAType::EmailBind;
    else if (lowerStr == "thirdpartybind") return eMFAType::ThirdPartyBind;
    else if (lowerStr == "phonechange") return eMFAType::PhoneChange;
    else if (lowerStr == "deleteuser") return eMFAType::DeleteUser;
    else if (lowerStr == "modifyuser") return eMFAType::ModifyUser;
    else if (lowerStr == "loginorregister") return eMFAType::LoginOrRegister;
    
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
        case eMFAType::ModifyUser: return "ModifyUser";
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

// 目前腾讯云添加的模板比较少, 所以多出来的新类型就都改成Default返回
eMFAType MFATypeToTencentSMSTemplateId(eMFAType type) {
    switch (type) {
        case eMFAType::Default: return type;
        case eMFAType::Login: return type;
        case eMFAType::Register: return type;
        case eMFAType::ResetPassword: return type;
        case eMFAType::EmailBind: return eMFAType::Default;
        case eMFAType::ThirdPartyBind: return eMFAType::Default;
        case eMFAType::PhoneChange: return eMFAType::Default;
        case eMFAType::DeleteUser: return eMFAType::Default;
        case eMFAType::ModifyUser: return eMFAType::Default;
        case eMFAType::LoginOrRegister: return eMFAType::Login;
        default: throw std::invalid_argument("转换到腾讯云MFAType失败: Unknown MFA type");
    }
}
#pragma once

#include <string>

// 枚举类型
enum class eMFAType : uint32_t {
    Default = 1 << 0,
    Login = 1 << 1,
    Register = 1 << 2,
    ResetPassword = 1 << 3,
    EmailBind = 1 << 4,
    PhoneChange = 1 << 5,
    DeleteUser = 1 << 6,
    LoginOrRegister = Login | Register
};

// 函数声明
eMFAType stringToMFAType(const std::string& str);
eMFAType operator|(eMFAType a, eMFAType b);
bool operator&(eMFAType a, eMFAType b);

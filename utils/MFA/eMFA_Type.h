#pragma once

#include <string>
#include "utils/ServiceResponse.h"

// // eMFAType 的特化（假设这是一个枚举类型）
// template<>
// inline Json::Value SerializeData<eMFAType>(const eMFAType& data) {
//     Json::Value value(Json::objectValue);
//     value["MFAType"] = MFATypeToString(data);
//     return value;
// }

// 枚举类型
enum class eMFAType : uint32_t {
    Default = 1 << 0,
    Login = 1 << 1,
    Register = 1 << 2,
    ResetPassword = 1 << 3,
    EmailBind = 1 << 4,
    PhoneChange = 1 << 5,
    DeleteUser = 1 << 6
};

// 函数声明
// 将字符串转换为 eMFAType, 当修改枚举时请同步修改此函数
eMFAType stringToMFAType(const std::string& str);
// 枚举转换为字符串
std::string MFATypeToString(eMFAType type);
eMFAType operator|(eMFAType a, eMFAType b);
bool operator&(eMFAType a, eMFAType b);

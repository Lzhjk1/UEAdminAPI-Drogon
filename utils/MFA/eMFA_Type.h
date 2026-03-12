#pragma once

#include <string>


// // eMFAType 的特化（假设这是一个枚举类型）
// template<>
// inline Json::Value SerializeData<eMFAType>(const eMFAType& data) {
//     Json::Value value(Json::objectValue);
//     value["MFAType"] = MFATypeToString(data);
//     return value;
// }



// 枚举类型
enum class eMFAType : uint32_t {
    // 用于标记错误
    Error = 0,

    Default = 1 << 0,
    Login = 1 << 1,
    Register = 1 << 2,
    ResetPassword = 1 << 3,
    EmailBind = 1 << 4,
    ThirdPartyBind = 1 << 5,
    PhoneChange = 1 << 6,
    DeleteUser = 1 << 7,
    ModifyUser = 1 << 8,
    Unbind = 1 << 9,
    LoginOrRegister = (1 << 1) | (1 << 2)
};

// 函数声明
// 将字符串转换为 eMFAType, 当修改枚举时请同步修改此函数
eMFAType stringToMFAType(const std::string& str);
// 枚举转换为字符串, 当修改枚举时请同步修改此函数
std::string MFATypeToString(eMFAType type);
eMFAType operator|(eMFAType a, eMFAType b);
bool operator&(eMFAType a, eMFAType b);
eMFAType MFATypeToTencentSMSTemplateId(eMFAType type);

#pragma once
#include <iostream>

using namespace std;

enum class UserPrivileges{
    Default = 1 << 0,
    User = 1 << 1,
    Admin = 1 << 2
};

inline UserPrivileges stringToUserPrivileges(const string& str){
    if (str == "Default") return UserPrivileges::Default;
    else if (str == "User") return UserPrivileges::User;
    else if (str == "Admin") return UserPrivileges::Admin;
    throw invalid_argument("Unknown UserPrivileges type: " + str);
}

// inline UserPrivileges intToUserPrivileges(int i){
//     return static_cast<UserPrivileges>(i);
// }

inline UserPrivileges operator|(UserPrivileges lhs, UserPrivileges rhs){
    return static_cast<UserPrivileges>(static_cast<int>(lhs) | static_cast<int>(rhs));
}
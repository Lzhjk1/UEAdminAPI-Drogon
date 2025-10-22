#pragma once

#include <iostream>
#include <optional>

using namespace std;

namespace ServiceDtos {
	class RegisterUserDto {
    public:
        string UserName, Password;
        optional<string> NickName, Email, PhoneNumber;
        optional<bool> isMale;

        explicit RegisterUserDto(const string& userName, const string& password) : UserName(userName), Password(password) {
            NickName = UserName;
            Email = "";
            PhoneNumber = "";
            isMale = true;
        }

        RegisterUserDto& withNickName(const string& nickName) { NickName = nickName; return *this; }
        RegisterUserDto& withEmail(const string& email) { Email = email; return *this; }
        RegisterUserDto& withPhoneNumber(const string& phoneNumber) { PhoneNumber = phoneNumber; return *this; }
        RegisterUserDto& withGender(bool male) { isMale = male; return *this; }
    };
}
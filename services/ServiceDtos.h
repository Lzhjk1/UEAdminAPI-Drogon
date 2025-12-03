#pragma once

#include <iostream>
#include <optional>

namespace ServiceDtos {
	class RegisterUserDto {
    public:
        std::string UserName, Password;
        std::optional<std::string> NickName, Email, PhoneNumber;
        std::optional<bool> isMale;

        explicit RegisterUserDto(const std::string& userName, const std::string& password) : UserName(userName), Password(password) {
            NickName = UserName;
            Email = "";
            PhoneNumber = "";
            isMale = true;
        }

        RegisterUserDto& withNickName(const std::string& nickName) { NickName = nickName; return *this; }
        RegisterUserDto& withEmail(const std::string& email) { Email = email; return *this; }
        RegisterUserDto& withPhoneNumber(const std::string& phoneNumber) { PhoneNumber = phoneNumber; return *this; }
        RegisterUserDto& withGender(bool male) { isMale = male; return *this; }
    };
}
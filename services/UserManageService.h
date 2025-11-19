#pragma once

#include <iostream>
#include "utils/Singleton.h"

#include "ServiceDtos.h"

using namespace ServiceDtos;

class IUserManageService{
    public:
    virtual std::tuple<bool, std::string> RegisterUser(const RegisterUserDto&) = 0;
};

class UserManageService : public IUserManageService {

public:
    std::tuple<bool, std::string> RegisterUser(const RegisterUserDto&) override;
};
using UserManageServiceSPtr = SingletonPtr<UserManageService>;


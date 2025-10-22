#pragma once

#include <iostream>
#include "utils/Singleton.h"
#include "utils/ServiceResponse.h"
#include "ServiceDtos.h"

using namespace ServiceDtos;

class IUserManageService{
    public:
    virtual ServiceResponse<int> RegisterUser(const RegisterUserDto&) = 0;
};

class UserManageService : public IUserManageService {

public:
    ServiceResponse<int> RegisterUser(const RegisterUserDto&) override;
};
using UserManageServiceSPtr = SingletonPtr<UserManageService>;


#include <drogon/drogon.h>

#include "UserManageService.h"

ServiceResponse<int> UserManageService::RegisterUser(const RegisterUserDto &) {
    
    drogon::app().getCustomConfig();
    return ServiceResponse<int>();
}
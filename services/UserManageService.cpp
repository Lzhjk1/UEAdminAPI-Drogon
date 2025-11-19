#include <drogon/drogon.h>

#include "UserManageService.h"

std::tuple<bool, std::string> UserManageService::RegisterUser(const RegisterUserDto &) {
   
    drogon::app().getCustomConfig();
    return {true, "ok"};
}
#pragma once
#include "utils/SingletonWithInit.h"
#include "utils/HttpResult.h"
#include <drogon/drogon.h>

class SystemService : public SingletonWithInit<SystemService> {
    friend class SingletonWithInit<SystemService>;
private:
    SystemService() = default;

public:
    drogon::Task<UEAdminAPI::utils::HttpResult> Ping();
};

#pragma once
#include "utils/SingletonWithInit.h"
#include "utils/HttpResult.h"
#include <drogon/drogon.h>

class SystemService : public SingletonWithInit<SystemService> {
    friend class SingletonWithInit<SystemService>;

public:
    SystemService();
    drogon::Task<UEAdminAPI::utils::HttpResult> Ping();
};

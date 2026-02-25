#include "SystemService.h"
#include <ctime>

using namespace UEAdminAPI::utils;

drogon::Task<HttpResult> SystemService::Ping() {
    HttpResult result;
    result.code = 0;
    result.msg = "pong";
    result.jsondata["serverTime"] = (Json::UInt64)time(nullptr);
    co_return result;
}

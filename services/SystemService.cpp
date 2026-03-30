#include "SystemService.h"
#include <ctime>
#include <trantor/utils/Logger.h>

using namespace UEAdminAPI::utils;

SystemService::SystemService() {
    LOG_INFO << "SystemService 初始化完成";
}

drogon::Task<HttpResult> SystemService::Ping() {
    HttpResult result;
    result.code = 0;
    result.msg = "pong";
    result.jsondata["serverTime"] = (Json::UInt64)time(nullptr);
    co_return result;
}

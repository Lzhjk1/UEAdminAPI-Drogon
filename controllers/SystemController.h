#pragma once
#include <drogon/HttpController.h>

using namespace drogon;

class SystemController : public drogon::HttpController<SystemController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(SystemController::Ping, "/system/ping", Get);
    // SQLite 基础健康检查接口, 跑一条 SELECT 1 确认 SQLiteService 工作正常
    ADD_METHOD_TO(SystemController::SqlitePing, "/system/sqlite/ping", Get);
    METHOD_LIST_END

    Task<HttpResponsePtr> Ping(HttpRequestPtr req);
    Task<HttpResponsePtr> SqlitePing(HttpRequestPtr req);
};

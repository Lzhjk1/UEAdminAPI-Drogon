#pragma once
#include <drogon/HttpController.h>

using namespace drogon;

class SystemController : public drogon::HttpController<SystemController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(SystemController::Ping, "/system/ping", Get);
    METHOD_LIST_END

    Task<HttpResponsePtr> Ping(HttpRequestPtr req);
};

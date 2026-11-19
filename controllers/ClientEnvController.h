#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

// 客户端环境信息：登录后上报，厂商超级管理员查看
class ClientEnvController : public drogon::HttpController<ClientEnvController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ClientEnvController::submit, "/api/client/env", Post, "AuthFilter");

    ADD_METHOD_TO(ClientEnvController::list, "/api/admin/client/env/list", Get,
                  "AuthFilter", "SuperAdminFilter");
    METHOD_LIST_END

    Task<HttpResponsePtr> submit(HttpRequestPtr req);
    Task<HttpResponsePtr> list(HttpRequestPtr req);
};

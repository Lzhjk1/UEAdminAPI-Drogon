#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

// 账号中心聚合：客户端登录后只调这一个接口就能拿到余额/团队/付费期/角色/授权
class MeController : public drogon::HttpController<MeController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(MeController::bootstrap, "/api/me/bootstrap", Get, "AuthFilter");
    METHOD_LIST_END

    Task<HttpResponsePtr> bootstrap(HttpRequestPtr req);
};

#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

// 团队管理：查看我的团队 / 团队管理员新建成员
// 注意：建号目前只走 AuthFilter（团队管理员未必绑定 MFA）；
//       后续接入 ActionToken 时在此加 "ActionTokenMiddleware" 并在 ActionTokenService 登记路由。
class TeamController : public drogon::HttpController<TeamController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(TeamController::getTeam, "/api/team", Get, "AuthFilter");
    ADD_METHOD_TO(TeamController::createMember, "/api/team/member", Post, "AuthFilter");
    METHOD_LIST_END

    Task<HttpResponsePtr> getTeam(HttpRequestPtr req);
    Task<HttpResponsePtr> createMember(HttpRequestPtr req);
};

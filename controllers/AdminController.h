#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

// 厂商超管：团队列表 / 付费期 / 云端功能授权
class AdminController : public drogon::HttpController<AdminController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AdminController::listTeams, "/api/admin/teams", Get, "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(AdminController::setSubscription, "/api/admin/teams/{1}/subscription", Put, "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(AdminController::setFeatureAuth, "/api/admin/teams/{1}/feature-auth", Put, "AuthFilter", "AdminFilter");

    // RBAC：角色 / 权限 / 权限组
    ADD_METHOD_TO(AdminController::listRoles, "/api/admin/roles", Get, "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(AdminController::createRole, "/api/admin/roles", Post, "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(AdminController::updateRole, "/api/admin/roles/{1}", Patch, "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(AdminController::deleteRole, "/api/admin/roles/{1}", Delete, "AuthFilter", "AdminFilter");

    ADD_METHOD_TO(AdminController::listPermissions, "/api/admin/permissions", Get, "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(AdminController::createPermission, "/api/admin/permissions", Post, "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(AdminController::updatePermission, "/api/admin/permissions/{1}", Patch, "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(AdminController::deletePermission, "/api/admin/permissions/{1}", Delete, "AuthFilter", "AdminFilter");

    ADD_METHOD_TO(AdminController::listGroups, "/api/admin/permission-groups", Get, "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(AdminController::createGroup, "/api/admin/permission-groups", Post, "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(AdminController::updateGroup, "/api/admin/permission-groups/{1}", Patch, "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(AdminController::deleteGroup, "/api/admin/permission-groups/{1}", Delete, "AuthFilter", "AdminFilter");
    METHOD_LIST_END

    Task<HttpResponsePtr> listTeams(HttpRequestPtr req);
    Task<HttpResponsePtr> setSubscription(HttpRequestPtr req, std::string teamId);
    Task<HttpResponsePtr> setFeatureAuth(HttpRequestPtr req, std::string teamId);

    Task<HttpResponsePtr> listRoles(HttpRequestPtr req);
    Task<HttpResponsePtr> createRole(HttpRequestPtr req);
    Task<HttpResponsePtr> updateRole(HttpRequestPtr req, std::string roleId);
    Task<HttpResponsePtr> deleteRole(HttpRequestPtr req, std::string roleId);

    Task<HttpResponsePtr> listPermissions(HttpRequestPtr req);
    Task<HttpResponsePtr> createPermission(HttpRequestPtr req);
    Task<HttpResponsePtr> updatePermission(HttpRequestPtr req, std::string permissionId);
    Task<HttpResponsePtr> deletePermission(HttpRequestPtr req, std::string permissionId);

    Task<HttpResponsePtr> listGroups(HttpRequestPtr req);
    Task<HttpResponsePtr> createGroup(HttpRequestPtr req);
    Task<HttpResponsePtr> updateGroup(HttpRequestPtr req, std::string groupId);
    Task<HttpResponsePtr> deleteGroup(HttpRequestPtr req, std::string groupId);
};

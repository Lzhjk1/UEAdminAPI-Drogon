#include "AdminController.h"

#include "services/RbacService.h"
#include "services/TeamService.h"
#include "utils/ApiErrorCodes.h"
#include "utils/HttpResult.h"
#include "utils/PostParamMap.h"

#include <string>

using namespace drogon;
using namespace UEAdminAPI;
using namespace UEAdminAPI::Services;
using namespace UEAdminAPI::utils;

namespace {
int CurrentUserId(const HttpRequestPtr &req)
{
    int userId = -1;
    auto attributes = req->getAttributes();
    if (attributes->find("userId"))
    {
        userId = attributes->get<int>("userId");
    }
    return userId;
}

HttpResponsePtr Respond(const HttpResult &result)
{
    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    return resp;
}

// permissionIds 允许 int 或数字字符串；present 表示请求里是否带了该字段
bool ParseIntArray(const Json::Value &json, const char *key, std::vector<int> &out, bool &present)
{
    present = json.isMember(key);
    if (!present)
    {
        return true;
    }
    const auto &node = json[key];
    if (!node.isArray())
    {
        return false;
    }
    for (const auto &item : node)
    {
        if (item.isInt())
        {
            out.push_back(item.asInt());
        }
        else if (item.isString())
        {
            try
            {
                out.push_back(std::stoi(item.asString()));
            }
            catch (...)
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    }
    return true;
}

int ParseTeamId(const std::string &teamId)
{
    try
    {
        return std::stoi(teamId);
    }
    catch (...)
    {
        return 0;
    }
}
} // namespace

Task<HttpResponsePtr> AdminController::listTeams(HttpRequestPtr req)
{
    const bool paidOnly = req->getParameter("paid") == "1" || req->getParameter("paid") == "true";
    const bool expiredOnly = req->getParameter("expired") == "1" || req->getParameter("expired") == "true";

    UEAdminAPI::utils::HttpResult result = TeamService::AdminListTeams(paidOnly, expiredOnly);
    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    co_return resp;
}

Task<HttpResponsePtr> AdminController::setSubscription(HttpRequestPtr req, std::string teamId)
{
    UEAdminAPI::utils::HttpResult result;
    const int id = ParseTeamId(teamId);
    if (id <= 0)
    {
        result.setResult(ApiErrorCode::ApiError_TeamNotFound);
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k200OK);
        co_return resp;
    }

    auto reqJson = req->getJsonObject();
    if (!reqJson)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidJsonFormat);
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k200OK);
        co_return resp;
    }

    PostParamMap pm;
    pm.addParam("startDate", true)
      .addParam("endDate", true)
      .addParam("plan", false);
    pm.readParamsFromJson(*reqJson);

    auto missings = pm.checkRequiredParams();
    if (!missings.empty())
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "缺少 startDate / endDate");
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k200OK);
        co_return resp;
    }

    result = TeamService::AdminSetSubscription(CurrentUserId(req), id,
                                               pm.getParam("startDate"),
                                               pm.getParam("endDate"),
                                               pm.getParam("plan"));
    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    co_return resp;
}

Task<HttpResponsePtr> AdminController::setFeatureAuth(HttpRequestPtr req, std::string teamId)
{
    UEAdminAPI::utils::HttpResult result;
    const int id = ParseTeamId(teamId);
    if (id <= 0)
    {
        result.setResult(ApiErrorCode::ApiError_TeamNotFound);
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k200OK);
        co_return resp;
    }

    auto reqJson = req->getJsonObject();
    if (!reqJson)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidJsonFormat);
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k200OK);
        co_return resp;
    }

    PostParamMap pm;
    pm.addParam("featureCode", true)
      .addParam("enabled", true);
    pm.readParamsFromJson(*reqJson);

    auto missings = pm.checkRequiredParams();
    if (!missings.empty())
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "缺少 featureCode / enabled");
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k200OK);
        co_return resp;
    }

    const std::string enabledStr = pm.getParam("enabled");
    const bool enabled = (enabledStr == "true" || enabledStr == "1");

    result = TeamService::AdminSetFeatureAuth(CurrentUserId(req), id,
                                              pm.getParam("featureCode"), enabled);
    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    co_return resp;
}

// ---------------- RBAC：角色 ----------------
Task<HttpResponsePtr> AdminController::listRoles(HttpRequestPtr req)
{
    (void)req;
    co_return Respond(RbacService::ListRoles());
}

Task<HttpResponsePtr> AdminController::createRole(HttpRequestPtr req)
{
    auto reqJson = req->getJsonObject();
    if (!reqJson)
    {
        HttpResult bad;
        bad.setResult(ApiErrorCode::ApiError_InvalidJsonFormat);
        co_return Respond(bad);
    }

    PostParamMap pm;
    pm.addParam("code", true).addParam("name", true).addParam("scope", false);
    pm.readParamsFromJson(*reqJson);
    if (!pm.checkRequiredParams().empty())
    {
        HttpResult bad;
        bad.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "缺少 code / name");
        co_return Respond(bad);
    }

    int scope = 2;
    if (!pm.getParam("scope").empty())
    {
        try
        {
            scope = std::stoi(pm.getParam("scope"));
        }
        catch (...)
        {
            scope = 2;
        }
    }

    co_return Respond(RbacService::CreateRole(pm.getParam("code"), pm.getParam("name"), scope));
}

Task<HttpResponsePtr> AdminController::updateRole(HttpRequestPtr req, std::string roleId)
{
    HttpResult result;
    const int id = ParseTeamId(roleId);
    if (id <= 0)
    {
        result.setResult(ApiErrorCode::ApiError_RoleNotFound);
        co_return Respond(result);
    }

    auto reqJson = req->getJsonObject();
    if (!reqJson)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidJsonFormat);
        co_return Respond(result);
    }

    std::vector<int> permissionIds;
    bool replace = false;
    if (!ParseIntArray(*reqJson, "permissionIds", permissionIds, replace))
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "permissionIds 必须是数组");
        co_return Respond(result);
    }

    const std::string name = (*reqJson)["name"].asString();
    if (name.empty() && !replace)
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "name / permissionIds 至少提供一个");
        co_return Respond(result);
    }

    co_return Respond(RbacService::UpdateRole(id, name, permissionIds, replace));
}

Task<HttpResponsePtr> AdminController::deleteRole(HttpRequestPtr req, std::string roleId)
{
    (void)req;
    const int id = ParseTeamId(roleId);
    if (id <= 0)
    {
        HttpResult bad;
        bad.setResult(ApiErrorCode::ApiError_RoleNotFound);
        co_return Respond(bad);
    }
    co_return Respond(RbacService::DeleteRole(id));
}

// ---------------- RBAC：权限 ----------------
Task<HttpResponsePtr> AdminController::listPermissions(HttpRequestPtr req)
{
    (void)req;
    co_return Respond(RbacService::ListPermissions());
}

Task<HttpResponsePtr> AdminController::createPermission(HttpRequestPtr req)
{
    auto reqJson = req->getJsonObject();
    if (!reqJson)
    {
        HttpResult bad;
        bad.setResult(ApiErrorCode::ApiError_InvalidJsonFormat);
        co_return Respond(bad);
    }

    PostParamMap pm;
    pm.addParam("code", true).addParam("module", true).addParam("action", true);
    pm.readParamsFromJson(*reqJson);
    if (!pm.checkRequiredParams().empty())
    {
        HttpResult bad;
        bad.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "缺少 code / module / action");
        co_return Respond(bad);
    }

    co_return Respond(RbacService::CreatePermission(pm.getParam("code"), pm.getParam("module"),
                                                    pm.getParam("action")));
}

Task<HttpResponsePtr> AdminController::updatePermission(HttpRequestPtr req, std::string permissionId)
{
    HttpResult result;
    const int id = ParseTeamId(permissionId);
    if (id <= 0)
    {
        result.setResult(ApiErrorCode::ApiError_PermissionNotFound);
        co_return Respond(result);
    }

    auto reqJson = req->getJsonObject();
    if (!reqJson)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidJsonFormat);
        co_return Respond(result);
    }

    co_return Respond(RbacService::UpdatePermission(id, (*reqJson)["module"].asString(),
                                                    (*reqJson)["action"].asString()));
}

Task<HttpResponsePtr> AdminController::deletePermission(HttpRequestPtr req, std::string permissionId)
{
    (void)req;
    const int id = ParseTeamId(permissionId);
    if (id <= 0)
    {
        HttpResult bad;
        bad.setResult(ApiErrorCode::ApiError_PermissionNotFound);
        co_return Respond(bad);
    }
    co_return Respond(RbacService::DeletePermission(id));
}

// ---------------- RBAC：权限组 ----------------
Task<HttpResponsePtr> AdminController::listGroups(HttpRequestPtr req)
{
    (void)req;
    co_return Respond(RbacService::ListGroups());
}

Task<HttpResponsePtr> AdminController::createGroup(HttpRequestPtr req)
{
    auto reqJson = req->getJsonObject();
    if (!reqJson)
    {
        HttpResult bad;
        bad.setResult(ApiErrorCode::ApiError_InvalidJsonFormat);
        co_return Respond(bad);
    }

    PostParamMap pm;
    pm.addParam("name", true);
    pm.readParamsFromJson(*reqJson);
    if (!pm.checkRequiredParams().empty())
    {
        HttpResult bad;
        bad.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "缺少 name");
        co_return Respond(bad);
    }

    std::vector<int> permissionIds;
    bool present = false;
    if (!ParseIntArray(*reqJson, "permissionIds", permissionIds, present))
    {
        HttpResult bad;
        bad.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "permissionIds 必须是数组");
        co_return Respond(bad);
    }

    co_return Respond(RbacService::CreateGroup(pm.getParam("name"), permissionIds));
}

Task<HttpResponsePtr> AdminController::updateGroup(HttpRequestPtr req, std::string groupId)
{
    HttpResult result;
    const int id = ParseTeamId(groupId);
    if (id <= 0)
    {
        result.setResult(ApiErrorCode::ApiError_PermissionGroupNotFound);
        co_return Respond(result);
    }

    auto reqJson = req->getJsonObject();
    if (!reqJson)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidJsonFormat);
        co_return Respond(result);
    }

    std::vector<int> permissionIds;
    bool replace = false;
    if (!ParseIntArray(*reqJson, "permissionIds", permissionIds, replace))
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "permissionIds 必须是数组");
        co_return Respond(result);
    }

    const std::string name = (*reqJson)["name"].asString();
    if (name.empty() && !replace)
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "name / permissionIds 至少提供一个");
        co_return Respond(result);
    }

    co_return Respond(RbacService::UpdateGroup(id, name, permissionIds, replace));
}

Task<HttpResponsePtr> AdminController::deleteGroup(HttpRequestPtr req, std::string groupId)
{
    (void)req;
    const int id = ParseTeamId(groupId);
    if (id <= 0)
    {
        HttpResult bad;
        bad.setResult(ApiErrorCode::ApiError_PermissionGroupNotFound);
        co_return Respond(bad);
    }
    co_return Respond(RbacService::DeleteGroup(id));
}

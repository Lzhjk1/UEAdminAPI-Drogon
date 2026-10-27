#include "RbacService.h"

#include <drogon/drogon.h>

#include "utils/ApiErrorCodes.h"

#include <sstream>

using namespace drogon;
using namespace UEAdminAPI;
using namespace UEAdminAPI::utils;

namespace UEAdminAPI {
namespace Services {

namespace {

const char *kBuiltinRoleCodes[] = {"super_admin", "team_admin", "engineer", "pm"};

bool IsBuiltinRole(const std::string &code)
{
    for (const char *c : kBuiltinRoleCodes)
    {
        if (code == c)
        {
            return true;
        }
    }
    return false;
}

std::string ToPgIntArray(const std::vector<int> &ids)
{
    std::ostringstream oss;
    oss << "{";
    for (size_t i = 0; i < ids.size(); ++i)
    {
        if (i > 0)
        {
            oss << ",";
        }
        oss << ids[i];
    }
    oss << "}";
    return oss.str();
}

Json::Value ParseJsonText(const std::string &text)
{
    Json::Value value;
    Json::Reader reader;
    if (!reader.parse(text, value))
    {
        return Json::Value(Json::arrayValue);
    }
    return value;
}

// 校验 permissions 集合都存在，返回不存在的个数
int CountMissingPermissions(const std::shared_ptr<drogon::orm::DbClient> &db, const std::vector<int> &ids)
{
    if (ids.empty())
    {
        return 0;
    }
    const std::string literal = ToPgIntArray(ids);
    auto rows = db->execSqlSync(
        "select count(*) as n from permission where id = any($1::int[])", literal);
    const int found = rows.empty() ? 0 : rows[0]["n"].as<int>();
    return static_cast<int>(ids.size()) - found;
}

HttpResult DatabaseError(const char *where, const drogon::orm::DrogonDbException &e)
{
    LOG_ERROR << where << " 失败: " << e.base().what();
    HttpResult result;
    result.setResult(ApiErrorCode::ApiError_DatabaseError);
    return result;
}

} // namespace

HttpResult RbacService::ListRoles()
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync(
            "select r.id, r.code, r.name, r.scope, "
            "       coalesce((select json_agg(rp.permission_id order by rp.permission_id) "
            "                 from role_permission rp where rp.role_id = r.id), '[]'::json)::text as permission_ids "
            "from role r order by r.id");

        Json::Value roles(Json::arrayValue);
        for (const auto &r : rows)
        {
            Json::Value role;
            role["id"] = r["id"].as<int>();
            role["code"] = r["code"].as<std::string>();
            role["name"] = r["name"].as<std::string>();
            role["scope"] = r["scope"].as<int>();
            role["builtin"] = IsBuiltinRole(r["code"].as<std::string>());
            role["permissionIds"] = ParseJsonText(r["permission_ids"].as<std::string>());
            roles.append(role);
        }
        result.jsondata["roles"] = roles;
        result.setResult(ApiErrorCode::ApiError_Success, "success");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        return DatabaseError("ListRoles", e);
    }
    return result;
}

HttpResult RbacService::CreateRole(const std::string &code, const std::string &name, int scope)
{
    HttpResult result;
    if (code.empty() || name.empty())
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "code / name 不能为空");
        return result;
    }
    if (scope != 1 && scope != 2)
    {
        scope = 2;
    }

    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync(
            "insert into role (code, name, scope) values ($1, $2, $3::int) "
            "on conflict (code) do nothing returning id, code, name, scope",
            code, name, scope);
        if (rows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_CodeAlreadyExists);
            return result;
        }
        result.jsondata["role"]["id"] = rows[0]["id"].as<int>();
        result.jsondata["role"]["code"] = rows[0]["code"].as<std::string>();
        result.jsondata["role"]["name"] = rows[0]["name"].as<std::string>();
        result.jsondata["role"]["scope"] = rows[0]["scope"].as<int>();
        result.jsondata["role"]["builtin"] = false;
        result.jsondata["role"]["permissionIds"] = Json::Value(Json::arrayValue);
        result.setResult(ApiErrorCode::ApiError_Success, "角色已创建");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        return DatabaseError("CreateRole", e);
    }
    return result;
}

HttpResult RbacService::UpdateRole(int roleId, const std::string &name,
                                   const std::vector<int> &permissionIds, bool replacePermissions)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync("select id, code from role where id = $1", roleId);
        if (rows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_RoleNotFound);
            return result;
        }

        if (!name.empty())
        {
            db->execSqlSync("update role set name = $2 where id = $1", roleId, name);
        }

        if (replacePermissions)
        {
            const int missing = CountMissingPermissions(db, permissionIds);
            if (missing > 0)
            {
                result.setResult(ApiErrorCode::ApiError_PermissionNotFound);
                return result;
            }
            const std::string literal = ToPgIntArray(permissionIds);
            db->execSqlSync(
                "with cleared as (delete from role_permission where role_id = $1) "
                "insert into role_permission (role_id, permission_id) "
                "select $1, unnest($2::int[])",
                roleId, literal);
        }

        auto updated = db->execSqlSync(
            "select r.id, r.code, r.name, r.scope, "
            "       coalesce((select json_agg(rp.permission_id order by rp.permission_id) "
            "                 from role_permission rp where rp.role_id = r.id), '[]'::json)::text as permission_ids "
            "from role r where r.id = $1",
            roleId);
        if (!updated.empty())
        {
            const auto &r = updated[0];
            result.jsondata["role"]["id"] = r["id"].as<int>();
            result.jsondata["role"]["code"] = r["code"].as<std::string>();
            result.jsondata["role"]["name"] = r["name"].as<std::string>();
            result.jsondata["role"]["scope"] = r["scope"].as<int>();
            result.jsondata["role"]["builtin"] = IsBuiltinRole(r["code"].as<std::string>());
            result.jsondata["role"]["permissionIds"] = ParseJsonText(r["permission_ids"].as<std::string>());
        }
        result.setResult(ApiErrorCode::ApiError_Success, "角色已更新");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        return DatabaseError("UpdateRole", e);
    }
    return result;
}

HttpResult RbacService::DeleteRole(int roleId)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync("select code, scope from role where id = $1", roleId);
        if (rows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_RoleNotFound);
            return result;
        }
        // 系统角色（scope = 1）是厂商能力的载体，不允许通过普通 RBAC 删除
        if (IsBuiltinRole(rows[0]["code"].as<std::string>()) || rows[0]["scope"].as<int>() == 1)
        {
            result.setResult(ApiErrorCode::ApiError_SystemRoleProtected);
            return result;
        }

        auto used = db->execSqlSync(
            "select 1 from team_member where role_id = $1 limit 1", roleId);
        if (!used.empty())
        {
            result.setResult(ApiErrorCode::ApiError_ResourceInUse, "仍有团队成员使用该角色");
            return result;
        }

        db->execSqlSync(
            "with cleared as (delete from role_permission where role_id = $1) "
            "delete from role where id = $1",
            roleId);
        result.setResult(ApiErrorCode::ApiError_Success, "角色已删除");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        return DatabaseError("DeleteRole", e);
    }
    return result;
}

HttpResult RbacService::ListPermissions()
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync("select id, code, module, action from permission order by id");
        Json::Value permissions(Json::arrayValue);
        for (const auto &r : rows)
        {
            Json::Value item;
            item["id"] = r["id"].as<int>();
            item["code"] = r["code"].as<std::string>();
            item["module"] = r["module"].as<std::string>();
            item["action"] = r["action"].as<std::string>();
            permissions.append(item);
        }
        result.jsondata["permissions"] = permissions;
        result.setResult(ApiErrorCode::ApiError_Success, "success");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        return DatabaseError("ListPermissions", e);
    }
    return result;
}

HttpResult RbacService::CreatePermission(const std::string &code,
                                         const std::string &module,
                                         const std::string &action)
{
    HttpResult result;
    if (code.empty() || module.empty() || action.empty())
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "code / module / action 不能为空");
        return result;
    }

    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync(
            "insert into permission (code, module, action) values ($1, $2, $3) "
            "on conflict (code) do nothing returning id, code, module, action",
            code, module, action);
        if (rows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_CodeAlreadyExists);
            return result;
        }
        result.jsondata["permission"]["id"] = rows[0]["id"].as<int>();
        result.jsondata["permission"]["code"] = rows[0]["code"].as<std::string>();
        result.jsondata["permission"]["module"] = rows[0]["module"].as<std::string>();
        result.jsondata["permission"]["action"] = rows[0]["action"].as<std::string>();
        result.setResult(ApiErrorCode::ApiError_Success, "权限已创建");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        return DatabaseError("CreatePermission", e);
    }
    return result;
}

HttpResult RbacService::UpdatePermission(int permissionId,
                                         const std::string &module,
                                         const std::string &action)
{
    HttpResult result;
    if (module.empty() && action.empty())
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "module / action 至少提供一个");
        return result;
    }

    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync(
            "update permission set module = coalesce(nullif($2, ''), module), "
            "                     action = coalesce(nullif($3, ''), action) "
            "where id = $1 returning id, code, module, action",
            permissionId, module, action);
        if (rows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_PermissionNotFound);
            return result;
        }
        result.jsondata["permission"]["id"] = rows[0]["id"].as<int>();
        result.jsondata["permission"]["code"] = rows[0]["code"].as<std::string>();
        result.jsondata["permission"]["module"] = rows[0]["module"].as<std::string>();
        result.jsondata["permission"]["action"] = rows[0]["action"].as<std::string>();
        result.setResult(ApiErrorCode::ApiError_Success, "权限已更新");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        return DatabaseError("UpdatePermission", e);
    }
    return result;
}

HttpResult RbacService::DeletePermission(int permissionId)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync("select 1 from permission where id = $1", permissionId);
        if (rows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_PermissionNotFound);
            return result;
        }

        auto used = db->execSqlSync(
            "select 1 from role_permission where permission_id = $1 "
            "union all select 1 from group_permission where permission_id = $1 limit 1",
            permissionId);
        if (!used.empty())
        {
            result.setResult(ApiErrorCode::ApiError_ResourceInUse, "权限仍被角色或权限组引用");
            return result;
        }

        db->execSqlSync("delete from permission where id = $1", permissionId);
        result.setResult(ApiErrorCode::ApiError_Success, "权限已删除");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        return DatabaseError("DeletePermission", e);
    }
    return result;
}

HttpResult RbacService::ListGroups()
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync(
            "select g.id, g.name, "
            "       coalesce((select json_agg(gp.permission_id order by gp.permission_id) "
            "                 from group_permission gp where gp.group_id = g.id), '[]'::json)::text as permission_ids "
            "from permission_group g order by g.id");

        Json::Value groups(Json::arrayValue);
        for (const auto &r : rows)
        {
            Json::Value group;
            group["id"] = r["id"].as<int>();
            group["name"] = r["name"].as<std::string>();
            group["permissionIds"] = ParseJsonText(r["permission_ids"].as<std::string>());
            groups.append(group);
        }
        result.jsondata["groups"] = groups;
        result.setResult(ApiErrorCode::ApiError_Success, "success");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        return DatabaseError("ListGroups", e);
    }
    return result;
}

HttpResult RbacService::CreateGroup(const std::string &name, const std::vector<int> &permissionIds)
{
    HttpResult result;
    if (name.empty())
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "name 不能为空");
        return result;
    }

    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync(
            "insert into permission_group (name) values ($1) "
            "on conflict (name) do nothing returning id, name",
            name);
        if (rows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_CodeAlreadyExists);
            return result;
        }
        const int groupId = rows[0]["id"].as<int>();

        if (!permissionIds.empty())
        {
            const int missing = CountMissingPermissions(db, permissionIds);
            if (missing > 0)
            {
                result.setResult(ApiErrorCode::ApiError_PermissionNotFound);
                return result;
            }
            const std::string literal = ToPgIntArray(permissionIds);
            db->execSqlSync(
                "insert into group_permission (group_id, permission_id) "
                "select $1, unnest($2::int[])",
                groupId, literal);
        }

        result.jsondata["group"]["id"] = groupId;
        result.jsondata["group"]["name"] = name;
        result.jsondata["group"]["permissionIds"] = Json::Value(Json::arrayValue);
        result.setResult(ApiErrorCode::ApiError_Success, "权限组已创建");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        return DatabaseError("CreateGroup", e);
    }
    return result;
}

HttpResult RbacService::UpdateGroup(int groupId, const std::string &name,
                                    const std::vector<int> &permissionIds, bool replacePermissions)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync("select id from permission_group where id = $1", groupId);
        if (rows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_PermissionGroupNotFound);
            return result;
        }

        if (!name.empty())
        {
            db->execSqlSync("update permission_group set name = $2 where id = $1", groupId, name);
        }

        if (replacePermissions)
        {
            const int missing = CountMissingPermissions(db, permissionIds);
            if (missing > 0)
            {
                result.setResult(ApiErrorCode::ApiError_PermissionNotFound);
                return result;
            }
            const std::string literal = ToPgIntArray(permissionIds);
            db->execSqlSync(
                "with cleared as (delete from group_permission where group_id = $1) "
                "insert into group_permission (group_id, permission_id) "
                "select $1, unnest($2::int[])",
                groupId, literal);
        }

        auto updated = db->execSqlSync(
            "select g.id, g.name, "
            "       coalesce((select json_agg(gp.permission_id order by gp.permission_id) "
            "                 from group_permission gp where gp.group_id = g.id), '[]'::json)::text as permission_ids "
            "from permission_group g where g.id = $1",
            groupId);
        if (!updated.empty())
        {
            result.jsondata["group"]["id"] = updated[0]["id"].as<int>();
            result.jsondata["group"]["name"] = updated[0]["name"].as<std::string>();
            result.jsondata["group"]["permissionIds"] =
                ParseJsonText(updated[0]["permission_ids"].as<std::string>());
        }
        result.setResult(ApiErrorCode::ApiError_Success, "权限组已更新");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        return DatabaseError("UpdateGroup", e);
    }
    return result;
}

HttpResult RbacService::DeleteGroup(int groupId)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync("select 1 from permission_group where id = $1", groupId);
        if (rows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_PermissionGroupNotFound);
            return result;
        }

        db->execSqlSync(
            "with cleared as (delete from group_permission where group_id = $1) "
            "delete from permission_group where id = $1",
            groupId);
        result.setResult(ApiErrorCode::ApiError_Success, "权限组已删除");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        return DatabaseError("DeleteGroup", e);
    }
    return result;
}

} // namespace Services
} // namespace UEAdminAPI

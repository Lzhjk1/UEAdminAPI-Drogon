#include "SuperAdminFilter.h"

#include "utils/ApiErrorCodes.h"
#include "utils/EnumUserPrivileges.h"
#include "utils/HttpResult.h"

#include <drogon/drogon.h>

using namespace drogon;
using namespace UEAdminAPI;

void SuperAdminFilter::doFilter(const HttpRequestPtr &req,
                                FilterCallback &&fcb,
                                FilterChainCallback &&fccb)
{
    int userId = -1;
    auto attributes = req->getAttributes();
    if (attributes->find("userId"))
    {
        userId = attributes->get<int>("userId");
    }

    if (userId <= 0)
    {
        UEAdminAPI::utils::HttpResult result;
        result.setResult(ApiErrorCode::ApiError_TokenMissing, "缺少登录态");
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k401Unauthorized);
        fcb(resp);
        return;
    }

    bool isSuperAdmin = false;
    try
    {
        auto db = drogon::app().getDbClient();
        auto rows = db->execSqlSync(
            "select (coalesce(u.privilege, 0) & $2::int) <> 0 as bit_admin, "
            "       exists(select 1 from user_system_role usr "
            "              join role r on r.id = usr.role_id "
            "              where usr.user_id = u.id and r.code = 'super_admin') as role_admin "
            "from \"user\" u where u.id = $1",
            userId, static_cast<int>(UserPrivileges::Admin));
        if (!rows.empty())
        {
            isSuperAdmin = rows[0]["bit_admin"].as<bool>() || rows[0]["role_admin"].as<bool>();
        }
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "SuperAdminFilter 查询权限失败: " << e.base().what();
    }

    if (!isSuperAdmin)
    {
        UEAdminAPI::utils::HttpResult result;
        result.setResult(ApiErrorCode::ApiError_AdminPrivilegeRequired, "仅厂商超级管理员可操作");
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k403Forbidden);
        fcb(resp);
        return;
    }

    fccb();
}

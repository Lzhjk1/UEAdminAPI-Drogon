#include "AdminFilter.h"
#include "utils/HttpResult.h"
#include "utils/ApiErrorCodes.h"
#include "utils/EnumUserPrivileges.h"
#include <drogon/drogon.h>

using namespace drogon;
using namespace UEAdminAPI;

void AdminFilter::doFilter(const HttpRequestPtr &req,
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

    bool isAdmin = false;
    try
    {
        auto db = drogon::app().getDbClient();
        auto rows = db->execSqlSync("select privilege from \"user\" where id = $1", userId);
        if (!rows.empty() && !rows[0]["privilege"].isNull())
        {
            int privilege = rows[0]["privilege"].as<int>();
            isAdmin = (privilege & static_cast<int>(UserPrivileges::Admin)) != 0;
        }
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "AdminFilter 查询权限失败: " << e.base().what();
    }

    if (!isAdmin)
    {
        UEAdminAPI::utils::HttpResult result;
        result.setResult(ApiErrorCode::ApiError_AdminPrivilegeRequired);
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k403Forbidden);
        fcb(resp);
        return;
    }

    fccb();
}

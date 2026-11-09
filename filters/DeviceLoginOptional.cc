#include "DeviceLoginOptional.h"

#include "services/DeviceLoginSessionService.h"
#include "utils/HttpResult.h"
#include "utils/ApiErrorCodes.h"
#include <drogon/HttpResponse.h>
#include <json/json.h>

void DeviceLoginOptional::doFilter(const HttpRequestPtr &req,
                                   FilterCallback &&fcb,
                                   FilterChainCallback &&fccb)
{
    auto userCode = req->getParameter("user_code");
    if (userCode.empty()) {
        // 兼容旧参数名，但它现在只按公开短码解析；旧 secret state 不会命中。
        userCode = req->getParameter("ueadmin_state");
    }
    if (userCode.empty()) {
        fccb();
        return;
    }

    auto sessionService = UEAdminAPI::Services::DeviceLoginSessionService::Instance();
    if (!sessionService || !sessionService->FindByUserCode(userCode)) {
        UEAdminAPI::utils::HttpResult result;
        result.setResult(UEAdminAPI::ApiErrorCode::ApiError_DeviceLoginStateInvalid,
                         "user_code 不存在、已过期或已消费");
        auto resp = drogon::HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(drogon::k400BadRequest);
        fcb(resp);
        return;
    }

    fccb();
}

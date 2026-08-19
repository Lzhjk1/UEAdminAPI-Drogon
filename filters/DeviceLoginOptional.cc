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
    auto state = req->getParameter("ueadmin_state");
    if (state.empty()) {
        fccb();
        return;
    }

    auto sessionService = UEAdminAPI::Services::DeviceLoginSessionService::Instance();
    if (!sessionService || !sessionService->FindSession(state)) {
        UEAdminAPI::utils::HttpResult result;
        result.setResult(UEAdminAPI::ApiErrorCode::ApiError_DeviceLoginStateInvalid,
                         "ueadmin_state 不存在、已过期或已消费");
        auto resp = drogon::HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(drogon::k400BadRequest);
        fcb(resp);
        return;
    }

    fccb();
}

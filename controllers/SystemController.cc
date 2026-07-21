#include "SystemController.h"
#include "services/SystemService.h"
#include "utils/HttpResult.h"

using namespace UEAdminAPI::utils;

Task<HttpResponsePtr> SystemController::Ping(HttpRequestPtr req) {
    auto _systemService = SystemService::Instance();

    HttpResult result = co_await _systemService->Ping();

    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);

    co_return resp;
}

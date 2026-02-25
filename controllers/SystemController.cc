#include "SystemController.h"
#include "services/SystemService.h"
#include "utils/HttpResult.h"

using namespace UEAdminAPI::utils;

Task<HttpResponsePtr> SystemController::Ping(HttpRequestPtr req) {
    auto _systemService = SystemService::Instance();

    auto resp = HttpResponse::newHttpResponse();
    
    HttpResult result = co_await _systemService->Ping();
    
    resp->setBody(result.toJsonString());
    resp->setStatusCode(k200OK);
    
    co_return resp;
}

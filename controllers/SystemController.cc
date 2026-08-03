#include "SystemController.h"
#include "services/SystemService.h"
#include "services/SQLiteService.h"
#include "utils/HttpResult.h"

using namespace UEAdminAPI::utils;

Task<HttpResponsePtr> SystemController::Ping(HttpRequestPtr req) {
    auto _systemService = SystemService::Instance();

    HttpResult result = co_await _systemService->Ping();

    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);

    co_return resp;
}

Task<HttpResponsePtr> SystemController::SqlitePing(HttpRequestPtr req) {
    // 取 SQLiteService 单例; 未初始化时 Instance() 会抛异常, 这里走默认 try
    auto resp = HttpResponse::newHttpResponse();
    HttpResult result;
    try {
        auto _sqliteService = UEAdminAPI::Services::SQLiteService::Instance();
        result = co_await _sqliteService->ping();
    } catch (const std::exception& e) {
        result.setResult(UEAdminAPI::ApiError_InternalError, std::string("SQLiteService 未就绪  : ") + e.what());
    }

    resp->setBody(result.toJsonString());
    resp->setStatusCode(k200OK);
    co_return resp;
}

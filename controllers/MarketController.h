#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

// 资源市场（Beta）：元件库/标准库文件包的列表、分片上传、下载与删除
class MarketController : public drogon::HttpController<MarketController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(MarketController::list, "/api/market/resources", Get, "AuthFilter");
    ADD_METHOD_TO(MarketController::download, "/api/market/resources/{1}/download", Get, "AuthFilter");
    ADD_METHOD_TO(MarketController::uploadInit, "/api/market/resources/upload/init", Post, "AuthFilter");
    ADD_METHOD_TO(MarketController::uploadChunk, "/api/market/resources/upload/{1}/chunk", Post, "AuthFilter");
    ADD_METHOD_TO(MarketController::uploadComplete, "/api/market/resources/upload/{1}/complete", Post, "AuthFilter");
    ADD_METHOD_TO(MarketController::remove, "/api/market/resources/{1}", Delete, "AuthFilter");
    METHOD_LIST_END

    Task<HttpResponsePtr> list(HttpRequestPtr req);
    Task<HttpResponsePtr> download(HttpRequestPtr req, std::string resourceId);
    Task<HttpResponsePtr> uploadInit(HttpRequestPtr req);
    Task<HttpResponsePtr> uploadChunk(HttpRequestPtr req, std::string uploadId);
    Task<HttpResponsePtr> uploadComplete(HttpRequestPtr req, std::string uploadId);
    Task<HttpResponsePtr> remove(HttpRequestPtr req, std::string resourceId);
};

#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

// 云工程：配额、元数据与预签名 URL。文件本体由客户端直传/直下存储服务。
class CloudProjectController : public drogon::HttpController<CloudProjectController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(CloudProjectController::status, "/api/cloud/status", Get, "AuthFilter");
    ADD_METHOD_TO(CloudProjectController::prepare, "/api/cloud/files/prepare", Post, "AuthFilter");
    ADD_METHOD_TO(CloudProjectController::complete, "/api/cloud/files/complete", Post, "AuthFilter");
    ADD_METHOD_TO(CloudProjectController::list, "/api/cloud/files", Get, "AuthFilter");
    ADD_METHOD_TO(CloudProjectController::download, "/api/cloud/files/download", Get, "AuthFilter");
    ADD_METHOD_TO(CloudProjectController::remove, "/api/cloud/files", Delete, "AuthFilter");
    METHOD_LIST_END

    Task<HttpResponsePtr> status(HttpRequestPtr req);
    Task<HttpResponsePtr> prepare(HttpRequestPtr req);
    Task<HttpResponsePtr> complete(HttpRequestPtr req);
    Task<HttpResponsePtr> list(HttpRequestPtr req);
    Task<HttpResponsePtr> download(HttpRequestPtr req);
    Task<HttpResponsePtr> remove(HttpRequestPtr req);
};

// Driver=local 时的直传/直下入口：凭短期签名参数访问，不使用登录令牌。
class CloudDirectController : public drogon::HttpController<CloudDirectController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(CloudDirectController::upload, "/api/cloud/direct", Put);
    ADD_METHOD_TO(CloudDirectController::download, "/api/cloud/direct", Get);
    METHOD_LIST_END

    void upload(const HttpRequestPtr &req,
                std::function<void(const HttpResponsePtr &)> &&callback);
    void download(const HttpRequestPtr &req,
                  std::function<void(const HttpResponsePtr &)> &&callback);
};

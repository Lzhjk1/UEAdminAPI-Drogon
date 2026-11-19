#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

// 安装包在线更新：客户端检查/下载，厂商后台上传/发布/列表
class UpdateController : public drogon::HttpController<UpdateController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(UpdateController::check, "/api/update/check", Get, "AuthFilter");
    ADD_METHOD_TO(UpdateController::download, "/api/update/download/{1}", Get, "AuthFilter");
    ADD_METHOD_TO(UpdateController::publicReleases, "/api/update/releases", Get, "RateLimitFilter");
    ADD_METHOD_TO(UpdateController::publicDownload, "/api/update/public/download/{1}", Get);
    ADD_METHOD_TO(UpdateController::deltaManifest, "/api/update/delta/{1}/manifest", Get, "AuthFilter");

    ADD_METHOD_TO(UpdateController::upload, "/api/admin/installer/upload", Post, "AuthFilter", "AdminFilter");
    // 分片上传（断点续传）：init -> chunk* -> complete
    ADD_METHOD_TO(UpdateController::uploadInit, "/api/admin/installer/upload/init", Post, "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(UpdateController::uploadChunk, "/api/admin/installer/upload/{1}/chunk", Post, "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(UpdateController::uploadComplete, "/api/admin/installer/upload/{1}/complete", Post, "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(UpdateController::publish, "/api/admin/installer/{1}/publish", Post, "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(UpdateController::offline, "/api/admin/installer/{1}/offline", Post, "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(UpdateController::remove, "/api/admin/installer/{1}", Delete, "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(UpdateController::update, "/api/admin/installer/{1}", Patch, "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(UpdateController::list, "/api/admin/installer/list", Get, "AuthFilter", "AdminFilter");

    // 差量包管理（上传复用 /api/admin/installer/upload/*，init 时带 kind=delta）
    ADD_METHOD_TO(UpdateController::publishDelta, "/api/admin/update/delta/{1}/publish", Post, "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(UpdateController::offlineDelta, "/api/admin/update/delta/{1}/offline", Post, "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(UpdateController::removeDelta, "/api/admin/update/delta/{1}", Delete, "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(UpdateController::listDeltas, "/api/admin/update/delta/list", Get, "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(UpdateController::deltaJobs, "/api/admin/update/delta/jobs", Get, "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(UpdateController::deltaHealth, "/api/admin/update/delta/health", Get, "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(UpdateController::deltaRebuild, "/api/admin/update/delta/rebuild", Post, "AuthFilter", "AdminFilter");

    // 极简上传页（页面本身不含数据，接口仍要求 Admin 权限）
    ADD_METHOD_TO(UpdateController::adminPage, "/admin/installer", Get);
    METHOD_LIST_END

    Task<HttpResponsePtr> check(HttpRequestPtr req);
    Task<HttpResponsePtr> download(HttpRequestPtr req, std::string version);
    Task<HttpResponsePtr> publicReleases(HttpRequestPtr req);
    Task<HttpResponsePtr> publicDownload(HttpRequestPtr req, std::string version);
    Task<HttpResponsePtr> deltaManifest(HttpRequestPtr req, std::string version);
    Task<HttpResponsePtr> upload(HttpRequestPtr req);
    Task<HttpResponsePtr> uploadInit(HttpRequestPtr req);
    Task<HttpResponsePtr> uploadChunk(HttpRequestPtr req, std::string uploadId);
    Task<HttpResponsePtr> uploadComplete(HttpRequestPtr req, std::string uploadId);
    Task<HttpResponsePtr> publish(HttpRequestPtr req, std::string installerId);
    Task<HttpResponsePtr> offline(HttpRequestPtr req, std::string installerId);
    Task<HttpResponsePtr> remove(HttpRequestPtr req, std::string installerId);
    Task<HttpResponsePtr> update(HttpRequestPtr req, std::string installerId);
    Task<HttpResponsePtr> list(HttpRequestPtr req);
    Task<HttpResponsePtr> publishDelta(HttpRequestPtr req, std::string deltaId);
    Task<HttpResponsePtr> offlineDelta(HttpRequestPtr req, std::string deltaId);
    Task<HttpResponsePtr> removeDelta(HttpRequestPtr req, std::string deltaId);
    Task<HttpResponsePtr> listDeltas(HttpRequestPtr req);
    Task<HttpResponsePtr> deltaJobs(HttpRequestPtr req);
    Task<HttpResponsePtr> deltaHealth(HttpRequestPtr req);
    Task<HttpResponsePtr> deltaRebuild(HttpRequestPtr req);
    Task<HttpResponsePtr> adminPage(HttpRequestPtr req);
};

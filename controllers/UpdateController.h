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

    ADD_METHOD_TO(UpdateController::upload, "/api/admin/installer/upload", Post, "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(UpdateController::publish, "/api/admin/installer/{1}/publish", Post, "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(UpdateController::list, "/api/admin/installer/list", Get, "AuthFilter", "AdminFilter");

    // 极简上传页（页面本身不含数据，接口仍要求 Admin 权限）
    ADD_METHOD_TO(UpdateController::adminPage, "/admin/installer", Get);
    METHOD_LIST_END

    Task<HttpResponsePtr> check(HttpRequestPtr req);
    Task<HttpResponsePtr> download(HttpRequestPtr req, std::string version);
    Task<HttpResponsePtr> upload(HttpRequestPtr req);
    Task<HttpResponsePtr> publish(HttpRequestPtr req, std::string installerId);
    Task<HttpResponsePtr> list(HttpRequestPtr req);
    Task<HttpResponsePtr> adminPage(HttpRequestPtr req);
};

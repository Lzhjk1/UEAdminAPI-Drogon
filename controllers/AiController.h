#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

// 内置 AI 模型代理：客户端令牌鉴权 + 订阅校验，模型 Key 只保存在服务端配置里。
// 请求/响应保持 OpenAI 兼容（messages / tools / choices），工具循环在客户端桥执行。
class AiController : public drogon::HttpController<AiController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AiController::chat, "/api/ai/chat", Post, "AuthFilter");
    ADD_METHOD_TO(AiController::usage, "/api/ai/usage", Get, "AuthFilter");

    // AI 服务管理（厂商管理控制台）
    ADD_METHOD_TO(AiController::adminOverview, "/api/admin/ai/overview", Get,
                  "AuthFilter", "VendorConsoleFilter");
    ADD_METHOD_TO(AiController::adminSettings, "/api/admin/ai/settings", Get,
                  "AuthFilter", "VendorConsoleFilter");
    ADD_METHOD_TO(AiController::adminSaveSettings, "/api/admin/ai/settings", Post,
                  "AuthFilter", "VendorConsoleFilter");
    ADD_METHOD_TO(AiController::adminUpstreams, "/api/admin/ai/upstreams", Get,
                  "AuthFilter", "VendorConsoleFilter");
    ADD_METHOD_TO(AiController::adminSaveUpstream, "/api/admin/ai/upstreams", Post,
                  "AuthFilter", "VendorConsoleFilter");
    ADD_METHOD_TO(AiController::adminDeleteUpstream, "/api/admin/ai/upstreams/delete", Post,
                  "AuthFilter", "VendorConsoleFilter");
    ADD_METHOD_TO(AiController::adminTeams, "/api/admin/ai/teams", Get,
                  "AuthFilter", "VendorConsoleFilter");
    ADD_METHOD_TO(AiController::adminBindByok, "/api/admin/ai/teams/byok", Post,
                  "AuthFilter", "VendorConsoleFilter");
    ADD_METHOD_TO(AiController::adminRemoveByok, "/api/admin/ai/teams/byok/remove", Post,
                  "AuthFilter", "VendorConsoleFilter");
    METHOD_LIST_END

    Task<HttpResponsePtr> chat(HttpRequestPtr req);

    // 用量查询：本人当月汇总；团队管理员额外返回团队汇总与按席位明细。
    Task<HttpResponsePtr> usage(HttpRequestPtr req);

    Task<HttpResponsePtr> adminOverview(HttpRequestPtr req);
    Task<HttpResponsePtr> adminSettings(HttpRequestPtr req);
    Task<HttpResponsePtr> adminSaveSettings(HttpRequestPtr req);
    Task<HttpResponsePtr> adminUpstreams(HttpRequestPtr req);
    Task<HttpResponsePtr> adminSaveUpstream(HttpRequestPtr req);
    Task<HttpResponsePtr> adminDeleteUpstream(HttpRequestPtr req);
    Task<HttpResponsePtr> adminTeams(HttpRequestPtr req);
    Task<HttpResponsePtr> adminBindByok(HttpRequestPtr req);
    Task<HttpResponsePtr> adminRemoveByok(HttpRequestPtr req);
};

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
    METHOD_LIST_END

    Task<HttpResponsePtr> chat(HttpRequestPtr req);

    // 用量查询：本人当月汇总；团队管理员额外返回团队汇总与按席位明细。
    Task<HttpResponsePtr> usage(HttpRequestPtr req);
};

#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

// 官网文档批注：匿名提交，管理员后台校对
class DocsFeedbackController : public drogon::HttpController<DocsFeedbackController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(DocsFeedbackController::submit, "/api/docs/feedback", Post, "RateLimitFilter");

    ADD_METHOD_TO(DocsFeedbackController::adminList, "/api/admin/docs-feedback/list", Get,
                  "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(DocsFeedbackController::adminHandle, "/api/admin/docs-feedback/{1}/handle", Post,
                  "AuthFilter", "AdminFilter");
    METHOD_LIST_END

    Task<HttpResponsePtr> submit(HttpRequestPtr req);
    Task<HttpResponsePtr> adminList(HttpRequestPtr req);
    Task<HttpResponsePtr> adminHandle(HttpRequestPtr req, std::string feedbackId);
};

#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

// 用户意见反馈：客户端提交，厂商超管查看/标记处理
class FeedbackController : public drogon::HttpController<FeedbackController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(FeedbackController::submit, "/api/feedback", Post, "AuthFilter");

    ADD_METHOD_TO(FeedbackController::list, "/api/admin/feedback/list", Get, "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(FeedbackController::image, "/api/admin/feedback/{1}/image", Get, "AuthFilter", "AdminFilter");
    ADD_METHOD_TO(FeedbackController::handle, "/api/admin/feedback/{1}/handle", Post, "AuthFilter", "AdminFilter");
    METHOD_LIST_END

    Task<HttpResponsePtr> submit(HttpRequestPtr req);
    Task<HttpResponsePtr> list(HttpRequestPtr req);
    Task<HttpResponsePtr> image(HttpRequestPtr req, std::string feedbackId);
    Task<HttpResponsePtr> handle(HttpRequestPtr req, std::string feedbackId);
};

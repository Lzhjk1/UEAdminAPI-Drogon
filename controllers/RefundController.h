#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

// 退款：用户申请 + 超级管理员审核/查看。管理端均要求 AuthFilter + SuperAdminFilter。
class RefundController : public drogon::HttpController<RefundController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(RefundController::apply, "/api/bill/order/{1}/refund", Post, "AuthFilter");
    ADD_METHOD_TO(RefundController::adminList, "/api/admin/refunds", Get, "AuthFilter", "SuperAdminFilter");
    ADD_METHOD_TO(RefundController::adminGet, "/api/admin/refunds/{1}", Get, "AuthFilter", "SuperAdminFilter");
    ADD_METHOD_TO(RefundController::adminApprove, "/api/admin/refunds/{1}/approve", Post, "AuthFilter", "SuperAdminFilter");
    ADD_METHOD_TO(RefundController::adminReject, "/api/admin/refunds/{1}/reject", Post, "AuthFilter", "SuperAdminFilter");
    ADD_METHOD_TO(RefundController::adminSync, "/api/admin/refunds/{1}/sync", Post, "AuthFilter", "SuperAdminFilter");
    ADD_METHOD_TO(RefundController::adminRollback, "/api/admin/refunds/{1}/rollback", Post, "AuthFilter", "SuperAdminFilter");
    METHOD_LIST_END

    Task<HttpResponsePtr> apply(HttpRequestPtr req, std::string orderNo);
    Task<HttpResponsePtr> adminList(HttpRequestPtr req);
    Task<HttpResponsePtr> adminGet(HttpRequestPtr req, std::string id);
    Task<HttpResponsePtr> adminApprove(HttpRequestPtr req, std::string id);
    Task<HttpResponsePtr> adminReject(HttpRequestPtr req, std::string id);
    Task<HttpResponsePtr> adminSync(HttpRequestPtr req, std::string id);
    Task<HttpResponsePtr> adminRollback(HttpRequestPtr req, std::string id);
};

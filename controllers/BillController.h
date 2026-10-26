#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

// 余额与支付：下单 / 轮询 / 流水 / 模拟到账 / 二维码占位图
class BillController : public drogon::HttpController<BillController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(BillController::createOrder, "/api/bill/order", Post, "AuthFilter");
    ADD_METHOD_TO(BillController::mockPaid, "/api/bill/order/{1}/mock-paid", Post, "AuthFilter");
    ADD_METHOD_TO(BillController::getOrder, "/api/bill/order/{1}", Get, "AuthFilter");
    ADD_METHOD_TO(BillController::listTransactions, "/api/bill/transactions", Get, "AuthFilter");
    // 二维码占位图不需要登录：图片按 orderNo 静态生成，不含任何用户数据
    ADD_METHOD_TO(BillController::qrPlaceholder, "/api/bill/qr/{1}", Get);
    METHOD_LIST_END

    Task<HttpResponsePtr> createOrder(HttpRequestPtr req);
    Task<HttpResponsePtr> getOrder(HttpRequestPtr req, std::string orderNo);
    Task<HttpResponsePtr> mockPaid(HttpRequestPtr req, std::string orderNo);
    Task<HttpResponsePtr> listTransactions(HttpRequestPtr req);
    Task<HttpResponsePtr> qrPlaceholder(HttpRequestPtr req, std::string orderNo);
};

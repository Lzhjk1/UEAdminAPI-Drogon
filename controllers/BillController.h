#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

// 余额与支付：下单 / 轮询 / 流水 / 模拟到账 / 二维码 / 渠道异步通知
class BillController : public drogon::HttpController<BillController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(BillController::createOrder, "/api/bill/order", Post, "AuthFilter");
    // 支付组：同时生成微信/支付宝两笔订单
    ADD_METHOD_TO(BillController::createOrderGroup, "/api/bill/order/group", Post, "AuthFilter");
    ADD_METHOD_TO(BillController::getOrderGroup, "/api/bill/order/group/{1}", Get, "AuthFilter");
    ADD_METHOD_TO(BillController::mockPaid, "/api/bill/order/{1}/mock-paid", Post, "AuthFilter");
    ADD_METHOD_TO(BillController::getOrder, "/api/bill/order/{1}", Get, "AuthFilter");
    ADD_METHOD_TO(BillController::listTransactions, "/api/bill/transactions", Get, "AuthFilter");
    ADD_METHOD_TO(BillController::listOrders, "/api/bill/orders", Get, "AuthFilter");
    // 二维码按 orderNo 静态生成，不含用户数据，不需要登录
    ADD_METHOD_TO(BillController::qrPlaceholder, "/api/bill/qr/{1}", Get);
    // 渠道异步通知：由支付宝/微信服务器直接 POST，不能经过 AuthFilter
    ADD_METHOD_TO(BillController::alipayNotify, "/api/bill/notify/alipay", Post);
    ADD_METHOD_TO(BillController::wechatNotify, "/api/bill/notify/wechat", Post);
    METHOD_LIST_END

    Task<HttpResponsePtr> createOrder(HttpRequestPtr req);
    Task<HttpResponsePtr> createOrderGroup(HttpRequestPtr req);
    Task<HttpResponsePtr> getOrderGroup(HttpRequestPtr req, std::string groupNo);
    Task<HttpResponsePtr> getOrder(HttpRequestPtr req, std::string orderNo);
    Task<HttpResponsePtr> mockPaid(HttpRequestPtr req, std::string orderNo);
    Task<HttpResponsePtr> listTransactions(HttpRequestPtr req);
    Task<HttpResponsePtr> listOrders(HttpRequestPtr req);
    Task<HttpResponsePtr> qrPlaceholder(HttpRequestPtr req, std::string orderNo);
    Task<HttpResponsePtr> alipayNotify(HttpRequestPtr req);
    Task<HttpResponsePtr> wechatNotify(HttpRequestPtr req);
};

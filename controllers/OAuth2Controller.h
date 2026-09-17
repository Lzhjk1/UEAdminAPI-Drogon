#pragma once
/// @file OAuth2Controller.h
/// @brief OAuth2 标准端点 — JWKS / Introspection / Revocation / 设备登录

#include <drogon/HttpController.h>
using namespace drogon;

class OAuth2Controller : public drogon::HttpController<OAuth2Controller> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(OAuth2Controller::jwks, "/.well-known/jwks.json", Get);
    ADD_METHOD_TO(OAuth2Controller::introspect, "/api/oauth2/introspect", Post);
    ADD_METHOD_TO(OAuth2Controller::revoke, "/api/oauth2/revoke", Post);
    ADD_METHOD_TO(OAuth2Controller::loginStart, "/api/oauth2/login/start", Post);
    ADD_METHOD_TO(OAuth2Controller::loginCheck, "/api/oauth2/login/check?state={1}", Get);
    ADD_METHOD_TO(OAuth2Controller::loginPage, "/login", Get);
    ADD_METHOD_TO(OAuth2Controller::loginByPwd, "/login", Post);
    ADD_METHOD_TO(OAuth2Controller::loginDone, "/login/done", Get);
    METHOD_LIST_END

    Task<HttpResponsePtr> jwks(HttpRequestPtr req);
    Task<HttpResponsePtr> introspect(HttpRequestPtr req);
    Task<HttpResponsePtr> revoke(HttpRequestPtr req);

    /// @brief POST /api/oauth2/login/start — 发起设备登录
    Task<HttpResponsePtr> loginStart(HttpRequestPtr req);

    /// @brief GET /api/oauth2/login/check?state= — 轮询登录状态
    Task<HttpResponsePtr> loginCheck(HttpRequestPtr req, std::string state);

    /// @brief GET /login — 设备登录页
    Task<HttpResponsePtr> loginPage(HttpRequestPtr req);

    /// @brief POST /login — 设备登录页账号密码登录
    Task<HttpResponsePtr> loginByPwd(HttpRequestPtr req);

    /// @brief GET /api/oauth2/login/done — 登录完成页（客户端 loopback 通知后再跳到这里展示）
    Task<HttpResponsePtr> loginDone(HttpRequestPtr req);
};

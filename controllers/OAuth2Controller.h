#pragma once
/// @file OAuth2Controller.h
/// @brief OAuth2 标准端点 — JWKS / Introspection / Revocation / RFC 8628 设备登录

#include <drogon/HttpController.h>
using namespace drogon;

class OAuth2Controller : public drogon::HttpController<OAuth2Controller> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(OAuth2Controller::jwks, "/.well-known/jwks.json", Get);
    ADD_METHOD_TO(OAuth2Controller::introspect, "/api/oauth2/introspect", Post);
    ADD_METHOD_TO(OAuth2Controller::revoke, "/api/oauth2/revoke", Post);
    // RFC 8628: device_authorization 返回 device_code/user_code；旧路径保留为别名。
    ADD_METHOD_TO(OAuth2Controller::loginStart, "/api/oauth2/device_authorization", Post);
    ADD_METHOD_TO(OAuth2Controller::loginStart, "/api/oauth2/login/start", Post);
    // RFC 8628 token endpoint：客户端用 device_code 轮询并领取 token。
    ADD_METHOD_TO(OAuth2Controller::deviceToken, "/api/oauth2/token", Post);
    ADD_METHOD_TO(OAuth2Controller::deviceVerificationPage, "/device", Get);
    ADD_METHOD_TO(OAuth2Controller::deviceVerificationSubmit, "/device", Post);
    ADD_METHOD_TO(OAuth2Controller::loginPage, "/login", Get);
    ADD_METHOD_TO(OAuth2Controller::loginByPwd, "/login", Post);
    ADD_METHOD_TO(OAuth2Controller::loginDone, "/login/done", Get);
    METHOD_LIST_END

    Task<HttpResponsePtr> jwks(HttpRequestPtr req);
    Task<HttpResponsePtr> introspect(HttpRequestPtr req);
    Task<HttpResponsePtr> revoke(HttpRequestPtr req);

    /// @brief POST /api/oauth2/device_authorization — 发起设备登录
    Task<HttpResponsePtr> loginStart(HttpRequestPtr req);

    /// @brief POST /api/oauth2/token — RFC 8628 设备码换 token
    Task<HttpResponsePtr> deviceToken(HttpRequestPtr req);

    /// @brief GET /device — 输入 user_code 的验证页
    Task<HttpResponsePtr> deviceVerificationPage(HttpRequestPtr req);

    /// @brief POST /device — 校验 user_code 后进入登录页
    Task<HttpResponsePtr> deviceVerificationSubmit(HttpRequestPtr req);

    /// @brief GET /login — 设备登录页
    Task<HttpResponsePtr> loginPage(HttpRequestPtr req);

    /// @brief POST /login — 设备登录页账号密码登录
    Task<HttpResponsePtr> loginByPwd(HttpRequestPtr req);

    /// @brief GET /login/done — 登录完成页
    Task<HttpResponsePtr> loginDone(HttpRequestPtr req);
};

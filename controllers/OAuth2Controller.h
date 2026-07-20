#pragma once
/// @file OAuth2Controller.h
/// @brief OAuth2 标准端点 — JWKS / Introspection / Revocation

#include <drogon/HttpController.h>
using namespace drogon;

class OAuth2Controller : public drogon::HttpController<OAuth2Controller> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(OAuth2Controller::jwks, "/.well-known/jwks.json", Get);
    ADD_METHOD_TO(OAuth2Controller::introspect, "/api/oauth2/introspect", Post);
    ADD_METHOD_TO(OAuth2Controller::revoke, "/api/oauth2/revoke", Post);
    METHOD_LIST_END

    Task<HttpResponsePtr> jwks(HttpRequestPtr req);
    Task<HttpResponsePtr> introspect(HttpRequestPtr req);
    Task<HttpResponsePtr> revoke(HttpRequestPtr req);
};

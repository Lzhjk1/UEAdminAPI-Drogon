#pragma once

#include <drogon/HttpController.h>
using namespace drogon;

class UserController : public drogon::HttpController<UserController> {
public:
    METHOD_LIST_BEGIN
    // 更新用户信息接口
    // 注意: 修改敏感信息时，需在请求头携带通过 /user/action_token 获取的 X-Action-Token
    ADD_METHOD_TO(UserController::updateUser, "/user/update", Post, "AuthFilter", "ActionTokenMiddleware");
    ADD_METHOD_TO(UserController::deleteUser, "/user/delete", Delete, "AuthFilter", "ActionTokenMiddleware");
    // 登录后使用的 ActionToken 获取接口
    ADD_METHOD_TO(UserController::generateActionToken, "/user/action_token?mfaType={1}&mfaCode={2}&target={3}", Get, "AuthFilter");
    // 登录前（或匿名状态下）使用的 ActionToken 获取接口
    ADD_METHOD_TO(UserController::generateActionTokenBeforeLogin, "/user/action_token/anonymous?mfaType={1}&mfaCode={2}&target={3}", Get);
    METHOD_LIST_END

    Task<HttpResponsePtr> updateUser(HttpRequestPtr req);

    // 删除当前登录用户
    // 现在不需要 query 参数了，因为已经被 ActionToken 替代
    Task<HttpResponsePtr> deleteUser(HttpRequestPtr req);

    // 获取操作授权令牌 (ActionToken) - 登录后
    Task<HttpResponsePtr> generateActionToken(HttpRequestPtr req, std::string mfaType, std::string mfaCode, std::string target);

    // 获取操作授权令牌 (ActionToken) - 登录前
    Task<HttpResponsePtr> generateActionTokenBeforeLogin(HttpRequestPtr req, std::string mfaType, std::string mfaCode, std::string target);
};

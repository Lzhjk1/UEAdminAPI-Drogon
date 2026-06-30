#pragma once

#include <drogon/HttpController.h>
using namespace drogon;

class UserController : public drogon::HttpController<UserController> {
public:
    METHOD_LIST_BEGIN
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

#pragma once

#include <drogon/HttpController.h>
using namespace drogon;

class UserController : public drogon::HttpController<UserController> {
public:
    METHOD_LIST_BEGIN
    // 更新用户信息接口
    // 特殊情况说明: 如果当前用户未绑定邮箱和电话(如第三方登录创建的初始账号), 
    // 则可以直接绑定邮箱或电话, 此时不需要提供 mfaCode 和 target 参数.
    ADD_METHOD_TO(UserController::updateUser, "/user/update", Post);
    ADD_METHOD_TO(UserController::deleteUser, "/user/delete?mfaCode={1}&target={2}", Delete);
    METHOD_LIST_END

    Task<HttpResponsePtr> updateUser(HttpRequestPtr req);
    Task<HttpResponsePtr> deleteUser(HttpRequestPtr req, const std::string mfaCode, const std::string target);
};

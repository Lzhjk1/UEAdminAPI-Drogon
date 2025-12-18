#pragma once

#include <drogon/HttpController.h>
using namespace drogon;

class UserController : public drogon::HttpController<UserController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(UserController::updateUser, "/user/update", Post);
    ADD_METHOD_TO(UserController::deleteUser, "/user/delete?verifyCode={1}&target={2}", Post);
    METHOD_LIST_END

    Task<HttpResponsePtr> updateUser(HttpRequestPtr req);
    Task<HttpResponsePtr> deleteUser(HttpRequestPtr req, const std::string verifyCode, const std::string target);
};

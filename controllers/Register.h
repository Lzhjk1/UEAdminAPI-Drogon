#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

class Register : public drogon::HttpController<Register>
{
  public:
    METHOD_LIST_BEGIN
    // use METHOD_ADD to add your custom processing function here;
    // METHOD_ADD(Register::get, "/{2}/{1}", Get); // path is /Register/{arg2}/{arg1}
    // METHOD_ADD(Register::your_method_name, "/{1}/{2}/list", Get); // path is /Register/{arg1}/{arg2}/list
    // ADD_METHOD_TO(Register::your_method_name, "/absolute/path/{1}/{2}/list", Get); // path is /absolute/path/{arg1}/{arg2}/list
    ADD_METHOD_TO(Register::RegisterUser, "/user/create", Post, "ActionTokenMiddleware");
    ADD_METHOD_TO(Register::RegisterUserByPhone, "/user/create/phone?phone={1}", Post, "ActionTokenMiddleware");
    ADD_METHOD_TO(Register::CheckUserExist, "/user/check_exist?target={1}", Get, "AuthFilter");

    METHOD_LIST_END
    // your declaration of processing function maybe like this:
    // void get(const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&callback, int p1, std::string p2);
    // void your_method_name(const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&callback, double p1, int p2) const;
    
    /// @brief 注册用户, 为避免无限注册，需要使用手机号或邮箱注册, 请求参数在Body里
    /// @param req 
    /// @return 成功时, 返回注册的用户的id
    Task<HttpResponsePtr> RegisterUser(HttpRequestPtr req);

    /// @brief 快速注册用户, 通过手机号和验证码注册, 自动生成用户名和密码
    Task<HttpResponsePtr> RegisterUserByPhone(HttpRequestPtr req, std::string phone);

    /// @brief 检查用户是否存在(通过邮箱或手机号)
    Task<HttpResponsePtr> CheckUserExist(HttpRequestPtr req, std::string target);




};

#pragma once

#include <drogon/HttpController.h>
#include "services/UserManageService.h"
#include "utils/MFA/eMFA_Type.h"

using namespace drogon;

class testclass {
public:
    std::string name;
    std::string name2;
};

template <>
eMFAType drogon::fromRequest<eMFAType>(const HttpRequest &req){
    return stringToMFAType(req.getParameter("mfaType"));
}

class Login : public drogon::HttpController<Login>
{
  public:
    METHOD_LIST_BEGIN
    // use METHOD_ADD to add your custom processing function here;
    // METHOD_ADD(Login::get, "/{2}/{1}", Get); // path is /Login/{arg2}/{arg1}
    // METHOD_ADD(Login::your_method_name, "/{1}/{2}/list", Get); // path is /Login/{arg1}/{arg2}/list
    // ADD_METHOD_TO(Login::your_method_name, "/absolute/path/{1}/{2}/list", Get); // path is /absolute/path/{arg1}/{arg2}/list
    ADD_METHOD_TO(Login::LoginByPwd, "/user/login/pwd?userName={1}&passWord={2}", Get);
    ADD_METHOD_TO(Login::LoginByEmail, "/user/login/email?email={1}&mfaCode={2}", Get);
    ADD_METHOD_TO(Login::LoginByPhone, "/user/login/phone?phone={1}&mfaCode={2}", Get);
    ADD_METHOD_TO(Login::LoginByFlashToken, "/user/login/flashtoken", Get);
    ADD_METHOD_TO(Login::VerifyToken, "/user/token/verify", Get);
    ADD_METHOD_TO(Login::GetSelfInfo, "/user/self", Get);
    ADD_METHOD_TO(Login::Logout, "/user/logout", Post);

    METHOD_LIST_END
    // your declaration of processing function maybe like this:
    // void get(const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&callback, int p1, std::string p2);
    // void your_method_name(const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&callback, double p1, int p2) const;


    /// @brief 通过密码登录
    Task<HttpResponsePtr> LoginByPwd(HttpRequestPtr req, std::string userName, std::string pwd);

    /// @brief 通过其他方式, 如邮箱, 手机验证码登录
    /// @param req 固定格式, 包含各种请求参数
    /// @param target 目标, 如邮箱, 手机号
    /// @param targetDBColName 目标在数据库的列名, 建议通过orm对象获取, 如User::Cols::_email
    /// @param mfaCode 预先通过验证码SendVerifyCode控制器发送的验证码
    /// @return 
    Task<HttpResponsePtr> LoginByOther(HttpRequestPtr req, std::string target, std::string targetDBColName, std::string mfaCode);

    /// @brief 通过邮箱验证码登录
    /// @param req 固定格式, 包含各种请求参数
    /// @param email 邮箱
    /// @param mfaCode 预先通过验证码SendVerifyCode控制器发送的验证码
    /// @return
    Task<HttpResponsePtr> LoginByEmail(HttpRequestPtr req, std::string email, std::string mfaCode);

    /// @brief 通过手机验证码登录
    /// @param req 固定格式, 包含各种请求参数
    /// @param phone 手机号
    /// @param mfaCode 预先通过验证码SendVerifyCode控制器发送的验证码
    /// @return
    Task<HttpResponsePtr> LoginByPhone(HttpRequestPtr req, std::string phone, std::string mfaCode);

    Task<HttpResponsePtr> LoginByFlashToken(HttpRequestPtr req);

    /// @brief 验证Token是否有效, 同时返回Token的相关信息
    /// @param req 
    /// @param token 要验证的Token
    /// @return 
    Task<HttpResponsePtr> VerifyToken(HttpRequestPtr req);

    Task<HttpResponsePtr> GetSelfInfo(HttpRequestPtr req);

    /// @brief 注销登录
    /// @param req 
    /// @return 
    Task<HttpResponsePtr> Logout(HttpRequestPtr req);


};

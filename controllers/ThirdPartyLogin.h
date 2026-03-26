#pragma once
#include <drogon/HttpController.h>
#include "services/ThirdPartyLoginService.h"

using namespace drogon;

namespace UEAdminAPI {
namespace Controllers {

class ThirdPartyLogin : public drogon::HttpController<ThirdPartyLogin> {
public:
    METHOD_LIST_BEGIN
    // 获取第三方登录URL
    ADD_METHOD_TO(ThirdPartyLogin::getLoginUrl, "/api/third/authorization_url?platform={1}", Get);
    // 第三方登录回调
    ADD_METHOD_TO(ThirdPartyLogin::callback, "/api/third/{1}?code={2}&state={3}", Get);
    // 验证第三方登录
    ADD_METHOD_TO(ThirdPartyLogin::verifyLogin, "/api/third/login/check?platform={1}&code={2}&verifyCode={3}&onlyCheck={4}", Get);
    // 直接通过第三方平台登录
    ADD_METHOD_TO(ThirdPartyLogin::loginWithThirdParty, "/api/third/login?platform={1}&code={2}&verifyCode={3}", Get);
    // 第三方绑定已有账号
    ADD_METHOD_TO(ThirdPartyLogin::bindAccount, "/api/third/bind?platform={1}&code={2}&verifyCode={3}", Post, "AuthFilter");
    // 第三方直接注册账号并完成登录
    ADD_METHOD_TO(ThirdPartyLogin::createUserFromThirdParty, "/api/third/register?platform={1}&code={2}&verifyCode={3}", Post);
    ADD_METHOD_TO(ThirdPartyLogin::unbindAccount, "/api/third/unbind?platform={1}&mfaTarget={2}&mfaCode={3}", Post, "AuthFilter");
    METHOD_LIST_END

    // GET
    // 获取第三方登录URL
    Task<HttpResponsePtr> getLoginUrl(HttpRequestPtr req, const std::string platform);

    // GET
    // 第三方登录回调
    // 实际上进入getLoginUrl接口返回的登录页面完成登录后, 浏览器会跳转到callback接口, 并获得code和state参数, 程序获取到之后就已经可以完成登录, 但是考虑到在AutoPDMS, 使用了一个独立的内置浏览器, 不便让该内置浏览器回传参数给AutoPDMS, 所以还需要AutoPDMS调用后续的verifyLogin接口来确认登录是否已经完成 
    // 这一步只是将LoginValue与第三方账号关联, 并没有绑定到某个用户
    Task<HttpResponsePtr> callback(HttpRequestPtr req, const std::string platform, const std::string code, const std::string state);

    // Get
    // 绑定已有账号
    Task<HttpResponsePtr> bindAccount(HttpRequestPtr req, const std::string platform, const std::string code, const std::string verifyCode);

    // GET
	// 验证第三方登录
    // 仅确认对应的code和verifyCode是否已经登陆
	Task<HttpResponsePtr> verifyLogin(HttpRequestPtr req, const std::string platform, const std::string code, const std::string verifyCode, const bool onlyCheck);

    // GET
    // 直接通过第三方平台登录
    Task<HttpResponsePtr> loginWithThirdParty(HttpRequestPtr req, const std::string platform, const std::string code, const std::string verifyCode);

    // POST
    // 从第三方账号注册账号, 不登录, 不消耗第三方登陆值, 以便注册好后直接使用原登录值登录
    Task<HttpResponsePtr> createUserFromThirdParty(HttpRequestPtr req, const std::string platform, const std::string code, const std::string verifyCode);

    Task<HttpResponsePtr> unbindAccount(HttpRequestPtr req, const std::string platform, const std::string mfaTarget, const std::string mfaCode);


};

} // namespace Controllers
} // namespace UEAdminAPI

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
    ADD_METHOD_TO(ThirdPartyLogin::getLoginUrl, "/api/third/{1}/login_url", Get);
    // 第三方登录回调
    ADD_METHOD_TO(ThirdPartyLogin::callback, "/api/third/{1}?code={2}&state={3}", Get);
    // 验证第三方登录
    ADD_METHOD_TO(ThirdPartyLogin::verifyLogin, "/api/third/verify?platform={1}&code={2}&verifyCode={3}", Get);
    METHOD_LIST_END

    // 获取第三方登录URL
    Task<HttpResponsePtr> getLoginUrl(HttpRequestPtr req,
		                            const std::string platform) const;

    // 第三方登录回调
    Task<HttpResponsePtr> callback(HttpRequestPtr req,
		                            const std::string platform,
		                            const std::string code,
		                            const std::string state) const;

	// 验证第三方登录
	Task<HttpResponsePtr> verifyLogin(HttpRequestPtr req,
		                            const std::string platform,
		                            const std::string code,
		                            const std::string verifyCode) const;

private:
    Services::ThirdPartyPlatform getPlatformFromString(const std::string& platform) const;
};

} // namespace Controllers
} // namespace UEAdminAPI

#include "Login.h"
#include "utils/HttpResult.h"
#include "models/User.h"
#include "services/AuthService.h"

using namespace UEAdminAPI;
using namespace UEAdminAPI::utils;
using namespace drogon_model::UEAdminAPI;
using namespace drogon::orm;
// Add definition of your processing function here

Task<HttpResponsePtr> Login::LoginByPwd(HttpRequestPtr req, std::string userName, std::string pwd) 
{
    // 依赖
    auto _authService = AuthService::Instance();

	auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k400BadRequest);

    HttpResult result;

    if (userName.empty() || pwd.empty()) {
        result.setResult(-1, "缺少用户名或密码");
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    auto dbClientPtr = drogon::app().getDbClient();

    drogon::orm::Mapper<User> mapperUsers(dbClientPtr);
    auto targetUser = mapperUsers.findFutureOne(Criteria(User::Cols::_name, CompareOperator::EQ, userName)).get();

    auto hash = targetUser.getPasswordHash();
    auto salt = targetUser.getPasswordSalt();
    if(!hash || !salt){
        result.setResult(-1, "用户没有设置密码");
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    if(!_authService->VerifyPasswordHash(pwd, hash, salt)){
        result.setResult(-1, "用户名或密码错误");
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    string token = _authService->CreateFlashToken(targetUser);
    result.jsondata["FlashToken"] = token;
    result.setResult(0, "登录成功");
    resp->setStatusCode(k200OK);

    resp->setBody(result.toJsonString());

	co_return resp;
}

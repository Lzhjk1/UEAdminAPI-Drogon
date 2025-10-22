#include "Login.h"
#include "utils/HttpResult.h"
#include "User.h"

using namespace UEAdminAPI::utils;
using namespace drogon_model::UEAdminAPI;
// Add definition of your processing function here

Task<HttpResponsePtr> Login::LoginByPwd(HttpRequestPtr req, std::string userName, std::string pwd,
    std::shared_ptr<UserManageService> _userManageService) 
{
	auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k400BadRequest);

    HttpResult result;

    if (userName.empty() || pwd.empty()) {
        result.setResult(2, "缺少用户名或密码");
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    auto dbClientPtr = drogon::app().getDbClient();

    drogon::orm::Mapper<User> mapperUsers(dbClientPtr);
    auto targetUser = mapperUsers.findByPrimaryKey(1);

    /*if (!user) {
        result->setResult(3, "用户名或密码错误");
        resp->setBody(result.toJsonString());
        co_return resp;
    }
    std::string encryptPwd = uehttp::DataFormatUtil::encryptPwd(userName, passWord);
    QTCH_LOG_DEBUG(logger) << "username=" << userName << " password=" << encryptPwd;
    if (encryptPwd != user->getUserPassword()) {
        result->setResult(3, "用户名或密码错误");
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    std::string token = UserDataMgr::GetInstance()->CreateToken(user->getId());
    std::string flashToken = UserDataMgr::GetInstance()->getFlashToken(user->getId());
    if (flashToken.empty()) {
        result->setResult(4, "flashtoken创建失败");
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    uesoft::im::ImLoginRecordInfo::ptr record = uesoft::im::ImLoginRecordInfo::ptr(new uesoft::im::ImLoginRecordInfo);
    record->setUserId(user->getId());
    record->setLogTime(time(0));
    record->setLoginIp(session->getRemoteAddressString());
    uesoft::im::ImLoginRecordInfoDao::Insert(record, pq_ptr);*/

    //result.jsondata["id"] = user->getId();
    //result.jsondata["token"] = token;
    //result.jsondata["flashToken"] = flashToken;

    resp->setBody(result.toJsonString());
    co_return resp;

	co_return resp;
}

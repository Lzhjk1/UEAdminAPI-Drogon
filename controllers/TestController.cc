#include "TestController.h"
#include "drogon/orm/DbClient.h"
#include <string>
#include <vector>
#include "utils/HttpResult.h"


#include "MfaChannels.h"
#include "MfaRecords.h"
#include "User.h"
#include "UserThirdpartyInfo.h"
#include "ThirdpartyPlatforms.h"
#include <plugins\SMTPMail.h>

using namespace drogon;
using namespace drogon::orm;
using namespace drogon_model::UEAdminAPI;
using namespace UEAdminAPI::utils;

TestController::TestController() {

}

Task<HttpResponsePtr> TestController::TestHandler(HttpRequestPtr req) {
	auto resp = HttpResponse::newHttpResponse();



	auto* smtpmailPtr = drogon::app().getPlugin<SMTPMail>();

	std::string id = co_await smtpmailPtr->sendEmailCoro(
		"smtp.qq.com",                  //The server IP/DNS
		587,                          //The port
		"3167832431@qq.com",       //Who send the email
		"1207629597@qq.com",    //Send to whom
		"Testing SMTPMail Function",  //Email Subject/Title
		"Hello from drogon plugin",   //Content
		"3167832431@qq.com",       //Login user
		"pbastjzdtcbadeaa",                     //User password
		false
	);
	resp->setBody(id);
	co_return resp;

	//auto dbClientPtr = drogon::app().getDbClient();

	//drogon::orm::Mapper<User> mapperUsers(dbClientPtr);
	//auto targetUser = mapperUsers.findByPrimaryKey(1);
	//auto res2 = targetUser.getThird_party_platforms(dbClientPtr);

	//drogon::orm::Mapper<MfaChannels> mapperMfaChannels(dbClientPtr);
	//MfaChannels channel;
	//channel.setChannelname("test");
	//auto rese = mapperMfaChannels.insertFuture(channel).get();

	//MfaChannels channel1 = mapperMfaChannels.findFutureByPrimaryKey(1).get();
	//std::vector<MfaRecords> records = channel1.getMfa_records(dbClientPtr);

	//drogon::orm::Mapper<ImUser> mapperTestUsers(dbClientPtr);

	////auto usersFound = co_await mapperTestUsers.findFutureBy(Criteria(ImUser::Cols::_id, CompareOperator::EQ, 204));
	//auto usersFound = mapperTestUsers.findBy(Criteria(ImUser::Cols::_id, CompareOperator::EQ, 204));
	//
	////mapperTestUsers.find

	//std::stringstream ss;
	//for (auto user : usersFound) {
	//	ss << "Name: " << user.getValueOfUserName() << std::endl;
	//}

	//// 处理结果...
	//for (auto& row : result) {
	//	for (auto& col : row) {
	//		ss << col.as<std::string>() << " ";
	//	}
	//}

	//auto authHeader = req->getHeader("Authorization");
	//// 提取Bearer令牌 (格式: "Bearer <token>")
	//if (authHeader.substr(0, 7) != "Bearer ") {
	//	throw std::runtime_error("Invalid Authorization header format");
	//}
	//std::string token = authHeader.substr(7);

	//auto res = new HttpResult();

	//resp->setBody(res->toJsonString());

	////std::this_thread::sleep_for(std::chrono::milliseconds(1000));



	co_return resp;
}

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

using namespace drogon;
using namespace drogon::orm;
using namespace drogon_model::UEAdminAPI;
using namespace UEAdminAPI::utils;

TestController::TestController() {

}

Task<HttpResponsePtr> TestController::TestHandler(HttpRequestPtr req) {
	auto resp = HttpResponse::newHttpResponse();

	// 打印当前请求的开始时间和线程ID
	auto start = std::chrono::system_clock::now();
	auto startTime = std::chrono::duration_cast<std::chrono::milliseconds>(start.time_since_epoch()).count();
	std::cout << "请求开始 | 时间: " << startTime << " | 线程ID: " << std::this_thread::get_id() << std::endl;

	auto dbClientPtr = drogon::app().getDbClient();

	drogon::orm::Mapper<User> mapperUsers(dbClientPtr);
	auto targetUser = mapperUsers.findByPrimaryKey(1);
	auto res2 = targetUser.getThird_party_platforms(dbClientPtr);

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

	auto res = new HttpResult();

	resp->setBody(res->toJsonString());

	//std::this_thread::sleep_for(std::chrono::milliseconds(1000));

	// 打印“发起数据库操作后立即返回”的时间（验证当前函数不阻塞）
	auto afterAsync = std::chrono::system_clock::now();
	auto afterTime = std::chrono::duration_cast<std::chrono::milliseconds>(afterAsync.time_since_epoch()).count();
	std::cout << "发起异步操作后返回 | 时间: " << afterTime << " | 线程ID: " << std::this_thread::get_id() << std::endl;


	co_return resp;
}

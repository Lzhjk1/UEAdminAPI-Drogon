#include "Register.h"
#include "models/User.h"
#include <utils/EnumUserPrivileges.h>
#include "services/AuthService.h"

using namespace std;
using namespace drogon;
using namespace drogon::orm;
using namespace drogon_model::UEAdminAPI;

struct param {
    string name;
    bool isExist;
    bool isNecessary;
    string value;
};

// Add definition of your processing function here
Task<HttpResponsePtr> Register::RegisterUser(HttpRequestPtr req) {
    // 依赖
    auto _authService = AuthService::Instance();

	auto resp = HttpResponse::newHttpResponse();

	auto reqJson = req->getJsonObject();
	auto members = reqJson->getMemberNames();

    // 使用param结构体存储参数信息
    unordered_map<string, param> mapParams = {
        {"username", {"username", false, true, ""}},
        {"password", {"password", false, true, ""}},
        {"privilege", {"privilege", false, false, ""}},
        {"nickname", {"nickname", false, false, ""}},
        {"email", {"email", false, false, ""}},
        {"phone", {"phone", false, false, ""}},
        {"isMale", {"isMale", false, false, ""}}
    };

	for (auto member : members) {
        auto it = mapParams.find(member);
        if (it != mapParams.end()) {
            it->second.isExist = true;  // 标记该字段存在
            // 存储字段的实际值
            if (reqJson->isMember(member)) {
                if ((*reqJson)[member].isString()) {
                    it->second.value = (*reqJson)[member].asString();
                } else {
                    // 非字符串类型转换为字符串
                    Json::StreamWriterBuilder builder;
                    builder["indentation"] = "";
                    it->second.value = Json::writeString(builder, (*reqJson)[member]);
                }
            }
        }
    }

    // 检查必填项是否都存在
    std::vector<std::string> missingFields;
    for (const auto &item : mapParams) {
        if (item.second.isNecessary && !item.second.isExist) {
            // 必填项不存在，添加到缺失列表中
            missingFields.push_back(item.first);
        }
    }
        
    // 通过检测email和phone字段来判断注册方式
    bool isEmailRegister = false;
    if(!(mapParams["email"].isExist || mapParams["phone"].isExist)) {
        missingFields.push_back("email or phone");
    }
    else if(mapParams["email"].isExist) {
        isEmailRegister = true;
    }

    // 如果有缺失的必填项，返回错误
    if (!missingFields.empty()) {
        resp->setStatusCode(k400BadRequest);
        Json::Value respJson;
        respJson["success"] = false;

        // 构建错误消息，列出所有缺失的必填项
        std::string missingFieldsStr;
        for (size_t i = 0; i < missingFields.size(); ++i) {
            if (i > 0) missingFieldsStr += ", ";
            missingFieldsStr += missingFields[i];
        }

        respJson["message"] = "缺少必填参数: " + missingFieldsStr;
        resp->setBody(respJson.toStyledString());
        co_return resp;
    }

    // 填充默认值
    if(!mapParams["privilege"].isExist) {
        mapParams["privilege"].value = "User";
    }
    if(!mapParams["nickname"].isExist) {
        mapParams["nickname"].value = mapParams["username"].value;
    }
    if(!mapParams["isMale"].isExist) {
        mapParams["isMale"].value = "true";
    }

    // 数据库相关初始化
    auto dbClientPtr = drogon::app().getDbClient();
    drogon::orm::Mapper<User> mapperUsers(dbClientPtr);

    //auto targetUser = mapperUsers.findFutureOne(Criteria(User::Cols::_name, CompareOperator::EQ, mapParams["username"].value)).get();
    // string strHash = *targetUser.getPasswordHash();
    //if(AuthRepository::VerifyPasswordHash(mapParams["password"].value,
    //                                     AuthRepository::stringToVector(*targetUser.getPasswordHash()),
    //                                     AuthRepository::stringToVector(*targetUser.getPasswordSalt()))) {
    //    LOG_INFO << "用户已存在，且密码正确";
    //    resp->setBody("{\"success\": true, \"userId\": " + to_string(*targetUser.getId()) + "}");
    //    co_return resp;
    //}

    // 检查用户名是否已存在
    User foundUser;
    bool isFound;
    try {
        foundUser = mapperUsers.findFutureOne(Criteria(User::Cols::_name, CompareOperator::EQ, mapParams["username"].value)).get();
        isFound = true;
    }
    catch (const drogon::orm::DrogonDbException& e) {
        // 有异常说明没找到
        isFound = false;
    }
    if (isFound) {
        LOG_ERROR << "用户名已存在";
        resp->setBody("{\"success\": false, \"message\": \"用户名已存在\"}");
        co_return resp;
    }
    // 检查邮箱是否已存在
    try{
        if(isEmailRegister){
            foundUser = mapperUsers.findFutureOne(Criteria(User::Cols::_email, CompareOperator::EQ, mapParams["email"].value)).get();
        }
        else{
            foundUser = mapperUsers.findFutureOne(Criteria(User::Cols::_telephone_number, CompareOperator::EQ, mapParams["phone"].value)).get();
        }
        isFound = true;
    }
    catch (const drogon::orm::DrogonDbException& e) {
        isFound = false;
    }
    if (isFound) {
        LOG_ERROR << "邮箱已存在";
        resp->setBody("{\"success\": false, \"message\": \"邮箱已存在\"}");
        co_return resp;
    }

    // 创建新用户
    auto newUser = User();
    newUser.setName(mapParams["username"].value);
    auto [passwordHash, passwordSalt] = _authService->CreatePasswordHash(mapParams["password"].value);
    newUser.setPasswordHash(_authService->vectorToString(passwordHash));
    newUser.setPasswordSalt(_authService->vectorToString(passwordSalt));
    newUser.setPrivilege(int(stringToUserPrivileges(mapParams["privilege"].value)));
    newUser.setNickName(mapParams["nickname"].value);
    newUser.setEmail(mapParams["email"].value);
    newUser.setTelephoneNumber(mapParams["phone"].value);
    newUser.setIsMale(mapParams["isMale"].value == "true" ? true : false);
    newUser.setCreateAt(trantor::Date::now());

    // TODO: 当前为阻塞式的插入，后续可改进
    auto future = mapperUsers.insertFuture(newUser);

    auto insertedUser = future.get();

    resp->setBody("{\"success\": true, \"userId\": " + to_string(*insertedUser.getId()) + "}");

    co_return resp;
}

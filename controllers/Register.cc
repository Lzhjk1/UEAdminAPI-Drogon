#include "Register.h"
#include "models/User.h"
#include <utils/EnumUserPrivileges.h>
#include "services/AuthService.h"
#include "services/MFAService.h"
#include <utils/PostParamMap.h>
#include <numeric>

using namespace std;
using namespace drogon;
using namespace drogon::orm;
using namespace drogon_model::UEAdminAPI;
using namespace UEAdminAPI;

// Add definition of your processing function here
Task<HttpResponsePtr> Register::RegisterUser(HttpRequestPtr req) {
    // 依赖
    auto _authService = AuthService::Instance();
    auto _mfaService = MFAService::Instance();

	auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);

	auto reqJson = req->getJsonObject();

    // 使用PostParamMap类处理参数
    PostParamMap paramMap;
    // 设置需要的参数
    paramMap.addParam("username", true)
        .addParam("password", true)
        .addParam("privilege", false, "User")
        .addParam("nickname", false)
        .addParam("email", false)
        .addParam("phone", false)
        .addParam("isMale", false, "true")
        .addParam("verifyCode", true);

    paramMap.readParamsFromJson(*reqJson);
    std::vector<std::string> missingFields = paramMap.checkRequiredParams();

    // 如果有缺失的必填项，返回错误
    if (!missingFields.empty()) {
        resp->setBody("{\"success\": false, \"message\": \"缺少必填项: " + std::accumulate(missingFields.begin(), missingFields.end(), std::string(), [](const std::string& a, const std::string& b) { return a + ", " + b; }) + "\"}");
        resp->setStatusCode(k400BadRequest);
        co_return resp;
    }

    // 通过检测email和phone字段来判断注册方式
    bool isEmailRegister = false;
    if(!(paramMap.hasParam("email") || paramMap.hasParam("phone"))) {
        missingFields.push_back("email or phone");
    }
    else if(paramMap.hasParam("email")) {
        isEmailRegister = true;
    }

    // 如果验证码检查失败
    if(!(co_await _mfaService->VerifyTheCode(paramMap.getParamValue("email"), paramMap.getParamValue("verifyCode"), eMFAType::Register)).Success) {
        resp->setBody("{\"success\": false, \"message\": \"验证码错误\"}");
        co_return resp;
    }

    // 设置昵称默认为用户名
    if(!paramMap.hasParam("nickname")) {
        paramMap.setParamValue("nickname", paramMap.getParamValue("username"));
    }

    // 数据库相关初始化
    auto dbClientPtr = drogon::app().getDbClient();
    drogon::orm::Mapper<User> mapperUsers(dbClientPtr);

    // 检查用户名是否已存在
    User foundUser;
    bool isFound;
    try {
        foundUser = mapperUsers.findFutureOne(Criteria(User::Cols::_name, CompareOperator::EQ, paramMap.getParamValue("username"))).get();
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
            foundUser = mapperUsers.findFutureOne(Criteria(User::Cols::_email, CompareOperator::EQ, paramMap.getParamValue("email"))).get();
        }
        else{
            foundUser = mapperUsers.findFutureOne(Criteria(User::Cols::_telephone_number, CompareOperator::EQ, paramMap.getParamValue("phone"))).get();
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
    newUser.setName(paramMap.getParamValue("username"));
    auto [passwordHash, passwordSalt] = _authService->CreatePasswordHash(paramMap.getParamValue("password"));
    newUser.setPasswordHash(_authService->vectorToString(passwordHash));
    newUser.setPasswordSalt(_authService->vectorToString(passwordSalt));
    newUser.setPrivilege(int(stringToUserPrivileges(paramMap.getParamValue("privilege"))));
    newUser.setNickName(paramMap.getParamValue("nickname"));
    newUser.setEmail(paramMap.getParamValue("email"));
    newUser.setTelephoneNumber(paramMap.getParamValue("phone"));
    newUser.setIsMale(paramMap.getParamValue("isMale") == "true" ? true : false);
    newUser.setCreateAt(trantor::Date::now());

    // TODO: 当前为阻塞式的插入，后续可改进
    auto insertedUser = mapperUsers.insertFuture(newUser).get();

    resp->setBody("{\"success\": true, \"userId\": " + to_string(*insertedUser.getId()) + "}");

    co_return resp;
}

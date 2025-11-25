#include "Register.h"
#include "models/User.h"
#include "models/UserGitlabInfo.h"
#include <utils/EnumUserPrivileges.h>
#include "services/AuthService.h"
#include "services/MFAService.h"
#include "services/GitlabService.h"
#include <utils/PostParamMap.h>
#include <utils/HttpResult.h>
#include <numeric>

using namespace std;
using namespace drogon;
using namespace drogon::orm;
using namespace drogon_model::UEAdminAPI;
using namespace UEAdminAPI;
using namespace UEAdminAPI::utils;

// Add definition of your processing function here
Task<HttpResponsePtr> Register::RegisterUser(HttpRequestPtr req) {
    // 依赖
    auto _authService = AuthService::Instance();
    auto _mfaService = MFAService::Instance();
    auto _gitlabService = GitlabService::Instance();


	auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    HttpResult result;

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
        HttpResult result;
        result.setResult(-1, "缺少必填项: " + std::accumulate(missingFields.begin(), missingFields.end(), std::string(), [](const std::string& a, const std::string& b) { return a + ", " + b; }));
        resp->setBody(result.toJsonString());
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
    auto [isSuccess, errMsg] = co_await _mfaService->VerifyTheCode(paramMap.getParamValue("email"), paramMap.getParamValue("verifyCode"), eMFAType::Register);
    if(!isSuccess) {
        result.setResult(-1, errMsg);
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    // 设置昵称默认为用户名
    if(!paramMap.hasParam("nickname")) {
        paramMap.setParamValue("nickname", paramMap.getParamValue("username"));
    }

    // 数据库相关初始化
    auto dbClientPtr = drogon::app().getDbClient();
    drogon::orm::Mapper<User> mapperUsers(dbClientPtr);
    drogon::orm::Mapper<UserGitlabInfo> mapperUserGitlabInfos(dbClientPtr);

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
        result.setResult(-1, "用户名已存在");
        resp->setBody(result.toJsonString());
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
        LOG_ERROR << std::format("邮箱: {} 已存在", paramMap.getParamValue("email"));
        result.setResult(-1, "邮箱已存在");
        resp->setBody(result.toJsonString());
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

    // 同步创建 GitLab 账号
    int gitlabUserId = 0;
    if(!_gitlabService->createUser(paramMap.getParamValue("username"), paramMap.getParamValue("password"), paramMap.getParamValue("email"), gitlabUserId)){
        result.setResult(-1, "创建 GitLab 账号失败");
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    // 邀请用户加入项目
    if(!_gitlabService->adminInvitationProject(1, UEAdminAPI::GitlabService::AccessLevels::Developer, gitlabUserId)){
        result.setResult(-1, "邀请用户加入项目失败");
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    // 创建gitlab impersonation token
    uint32_t gitImpersonationTokenId = 0;
    string gitlabImpersonationToken = "";
    if(!_gitlabService->createImpersonationToken(gitlabUserId, gitlabImpersonationToken, gitImpersonationTokenId)){
        result.setResult(-1, "创建 GitLab Impersonation Token失败");
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    // 插入数据库
    // TODO: 当前为阻塞式的插入，后续可改进
    // auto transaction = co_await dbClientPtr->newTransactionCoro();
    auto insertedUserFuture = mapperUsers.insertFuture(newUser);

    // 获取future
    bool step1Failed = false;
    bool step2Failed = false;

    User insertedUser;
    UserGitlabInfo insertedUserGitlabInfo;

    try{
        insertedUser = insertedUserFuture.get();
    }catch (const drogon::orm::DrogonDbException& e) {
        LOG_ERROR << "插入用户失败:" <<e.base().what();
        step1Failed = true;
    }

    // 创建用户gitlab信息数据库对象
    auto newUserGitlab = UserGitlabInfo();
    // 要先插入用户, 才能获得用户Id
    newUserGitlab.setUserId(insertedUser.getValueOfId());
    newUserGitlab.setGitId(gitlabUserId);
    newUserGitlab.setGitlabImpersonationToken(gitlabImpersonationToken);
    newUserGitlab.setGitlabImpersonationTokenId(gitImpersonationTokenId);
    auto insertedUserGitlabInfoFuture = mapperUserGitlabInfos.insertFuture(newUserGitlab);
    
    try{
        insertedUserGitlabInfo = insertedUserGitlabInfoFuture.get();
    }catch (const drogon::orm::DrogonDbException& e) {
        LOG_ERROR << "插入Gitlab信息失败:" <<e.base().what();
        step2Failed = true;
    }

    // 如有错误, 撤销操作 (drogon有提供执行sql时的事务操作, 但是mapper的没找到)
    if(step1Failed){
        if(!_gitlabService->deleteUser(gitlabUserId)){
            LOG_ERROR << "创建用户时失败, 回滚时删除Gitlab用户失败";
        }
        LOG_ERROR << "创建用户时失败, 回滚, 先前创建的Gitlab用户已成功删除";
        result.setResult(-1, "创建用户失败");
        resp->setBody(result.toJsonString());
        co_return resp;
    }
    if(!step1Failed && step2Failed){
        if(!_gitlabService->deleteUser(gitlabUserId)){
            LOG_ERROR << "创建用户时失败, 回滚时删除Gitlab用户失败";
        }
        else {
            LOG_ERROR << "创建用户时失败, 回滚, 先前创建的Gitlab用户已成功删除";
        }
        if(mapperUsers.deleteFutureOne(insertedUser).get() == 0){
            LOG_ERROR << "创建用户时失败, 回滚时删除先前插入的用户数据失败";
        }
        else {
            LOG_ERROR << "创建用户时失败, 回滚时删除先前插入的用户数据成功";
        }
        result.setResult(-1, "创建用户失败");
        resp->setBody(result.toJsonString());
        co_return resp;
    }
    
    result.setResult(0, "创建用户成功");
    result.jsondata["userId"] = *insertedUser.getId();
    resp->setBody(result.toJsonString());

    co_return resp;
}

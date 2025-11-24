#include "Login.h"
#include "utils/HttpResult.h"
#include "models/User.h"
#include "models/UserFlashtoken.h"
#include "services/AuthService.h"
#include "services/MFAService.h"
#include "utils/RandomGenerator.h"
#include <random>

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

    Mapper<User> mapperUsers(dbClientPtr);
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

    // 随机生成数字作为状态
    int status = RandomGenerator::getInt(1, INT_MAX);
    string token = _authService->CreateFlashToken(targetUser.getValueOfId(), status);
    // 更新状态到数据库
    Mapper<UserFlashtoken> mapperUserFlashtoken(dbClientPtr);
    UserFlashtoken flashTokenRow;
    // 是否需要新建行
    bool isNeedNewCreate = false;
    try {
        flashTokenRow = targetUser.getUser_flashtoken(dbClientPtr);
    } catch (UnexpectedRows& e){
        // 此外还有发现多个匹配行的错误
        if(strcmp(e.what(), "0 rows found") != 0){
            throw e;
        }
        isNeedNewCreate = true;
    }

    // 如果需要新建
    if (isNeedNewCreate) {
        UserFlashtoken newFlashtoken;
        newFlashtoken.setUserId(targetUser.getValueOfId());
        newFlashtoken.setStatus(status);
        
        try {
            auto ret = mapperUserFlashtoken.insertFuture(newFlashtoken).get();
        }
        catch (std::runtime_error& e){
            // TODO: 尚不清楚他会抛出什么错误
            LOG_ERROR << "新建flashtoken插入时出错!";
            throw e;
        }
    }
    // 如果新建了行, 就不需要修改了
    else {
        flashTokenRow.setStatus(status);
        auto ret = mapperUserFlashtoken.updateFuture(flashTokenRow).get();
        if (ret != 1) {
            result.setResult(-1, "更新状态失败");
            resp->setBody(result.toJsonString());
            co_return resp;
        }
    }

    result.jsondata["FlashToken"] = token;
    result.setResult(0, "登录成功");
    resp->setStatusCode(k200OK);

    resp->setBody(result.toJsonString());

	co_return resp;
}

Task<HttpResponsePtr> Login::LoginByOther(HttpRequestPtr req, std::string target, std::string targetDBColName, std::string verifyCode) {
    // 依赖
    auto _mfaService = MFAService::Instance();
    auto _authService = AuthService::Instance();

    // 用于返回结果的相关变量预定义
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    HttpResult result;

    // 检查验证码
    auto [isSuccess, errMsg] = co_await _mfaService->VerifyTheCode(target, verifyCode, eMFAType::Login);
    if(!isSuccess) {
        LOG_ERROR << errMsg;
        result.setResult(-1, "验证码错误");
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    // 获取dbClient并创建mapper, 固定用法
    auto dbClientPtr = drogon::app().getDbClient();
    drogon::orm::Mapper<User> mapperUsers(dbClientPtr);
    drogon::orm::Mapper<UserFlashtoken> mapperFlashToken(dbClientPtr);
    
    // 查找用户
    User targetUser;
    try{
        targetUser = mapperUsers.findFutureOne(Criteria(targetDBColName, CompareOperator::EQ, target)).get();
    }
    // 没找到会抛出异常
    catch (UnexpectedRows& e){
        LOG_ERROR << "邮箱验证码登录查询用户时出错!";
        result.setResult(-1, "用户不存在");
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    // 根据刚找到的用户的关系从数据库获取对应的user_flash_token的记录
    auto flashtoken = targetUser.getUser_flashtoken(dbClientPtr);
    
    // 创建新的FlashToken
    // 生成随机数作为status, status用于标记最新的token 
    //(虽说这样是抛弃了JWT无状态的优点, 但也只是存了一个int数, 开销不大, 免去了要处理同时有多个FlashToken可用的问题的麻烦)
    int status = RandomGenerator::getInt(1, INT_MAX);
    string token = _authService->CreateFlashToken(targetUser.getValueOfId(), status);
    // 更新状态到数据库
    flashtoken.setStatus(status);
    auto ret = mapperFlashToken.update(flashtoken);
    if (ret != 1) {
        result.setResult(-1, "更新状态失败");
        resp->setStatusCode(k500InternalServerError);
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    result.jsondata["FlashToken"] = token;
    result.setResult(0, "登录成功");
    resp->setBody(result.toJsonString());

    co_return resp;
}

Task<HttpResponsePtr> Login::LoginByEmail(HttpRequestPtr req, std::string email, std::string verifyCode){
    co_return co_await LoginByOther(req, email, User::Cols::_email, verifyCode);
}

Task<HttpResponsePtr> Login::LoginByPhone(HttpRequestPtr req, std::string phone, std::string verifyCode) {
    co_return co_await LoginByOther(req, phone, User::Cols::_telephone_number, verifyCode);
}

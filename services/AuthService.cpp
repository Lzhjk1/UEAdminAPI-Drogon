#include "AuthService.h"

#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/open-source-parsers-jsoncpp/defaults.h>
#include <jwt-cpp/traits/open-source-parsers-jsoncpp/traits.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include "services/AuthService.h"
#include "services/MFAService.h"
#include "services/GitLabService.h"

#include "models/UserFlashtoken.h"
#include "models/User.h"
#include "models/UserGitlabInfo.h"

#include <utils/EnumUserPrivileges.h>

using namespace drogon_model::UEAdminAPI;
using namespace drogon::orm;
using namespace UEAdminAPI;
using namespace UEAdminAPI::utils;

AuthService::AuthService(const Json::Value &config) {
    _secret = config["UserManage"]["jwt_secret"].asString();
    _jwtIssuer = config["UserManage"]["jwt_issuer"].asString();

    // 检查
    if (_secret.empty() || _jwtIssuer.empty()) {
        LOG_ERROR << "UserManage:jwt_secert or jwt_issuer unset";
        throw std::runtime_error("UserManage:jwt_secert or jwt_issuer unset");
    }

}

std::vector<unsigned char> AuthService::stringToVector(const std::string &str) {
    return std::vector<unsigned char>(str.begin(), str.end());
}
std::string AuthService::vectorToString(const std::vector<unsigned char> &vec) {
    return std::string(vec.begin(), vec.end());
}
std::string AuthService::vectorToString(const std::vector<char> &vec) {
    return std::string(vec.begin(), vec.end());
}

std::tuple<std::vector<unsigned char>, std::vector<unsigned char>> AuthService::CreatePasswordHash(const std::string &password) {
    // 生成随机 salt
    std::vector<unsigned char> salt(64);
    RAND_bytes(salt.data(), salt.size());

    // 计算 HMACSHA512
    unsigned int len = SHA512_DIGEST_LENGTH;
    std::vector<unsigned char> hash(len);
    HMAC(EVP_sha512(), salt.data(), salt.size(),
         reinterpret_cast<const unsigned char *>(password.data()), password.size(),
         hash.data(), &len);

    return std::make_tuple(hash, salt);
}

std::tuple<std::string, std::string> AuthService::CreateStrPasswordHash(const std::string &password) {
    auto [hash, salt] = CreatePasswordHash(password);
    return std::make_tuple(vectorToString(hash), vectorToString(salt));
}
bool AuthService::VerifyPasswordHash(const std::string &password,
                                        const std::vector<unsigned char> &hash,
                                        const std::vector<unsigned char> &salt) {
    unsigned int len = SHA512_DIGEST_LENGTH;
    std::vector<unsigned char> computedHash(len);
    HMAC(EVP_sha512(), salt.data(), salt.size(),
         reinterpret_cast<const unsigned char *>(password.data()), password.size(),
         computedHash.data(), &len);
    return computedHash == hash;
}

bool AuthService::VerifyPasswordHash(const std::string &password,
                                        const std::shared_ptr<std::vector<char>> &hashPtr,
                                        const std::shared_ptr<std::vector<char>> &saltPtr) {
    // 创建不拥有数据的vector视图，避免复制
    std::vector<unsigned char> hashView(
        reinterpret_cast<unsigned char *>(hashPtr->data()),
        reinterpret_cast<unsigned char *>(hashPtr->data() + hashPtr->size()));
    std::vector<unsigned char> saltView(
        reinterpret_cast<unsigned char *>(saltPtr->data()),
        reinterpret_cast<unsigned char *>(saltPtr->data() + saltPtr->size()));

    // 调用原始函数
    return VerifyPasswordHash(password, hashView, saltView);
}

std::string AuthService::CreateToken(int id, int status, uint64_t durationSeconds) {
    auto token = jwt::create()
                     .set_issuer(_jwtIssuer)
                     .set_type("JWS")
                     .set_payload_claim("id", jwt::claim(std::to_string(id)))
                     .set_payload_claim(std::string("tokenType"), jwt::claim(std::string("token")))
                     // status是附加验证信息
                     .set_payload_claim(std::string("status"), jwt::claim(std::to_string(status)))
                     .set_expires_in(std::chrono::seconds{durationSeconds})
                     .sign(jwt::algorithm::hs512{ _secret });
    return token;
}
std::string AuthService::CreateFlashToken(int id, int status, uint64_t durationSeconds) {
    auto token = jwt::create()
                     .set_issuer(_jwtIssuer)
                     .set_type("JWS")
                     .set_payload_claim("id", jwt::claim(std::to_string(id)))
                     .set_payload_claim(std::string("tokenType"), jwt::claim(std::string("flashToken")))
                     // status是附加验证信息
                     .set_payload_claim(std::string("status"), jwt::claim(std::to_string(status)))
                     .set_expires_in(std::chrono::seconds{durationSeconds})
                     .sign(jwt::algorithm::hs512{ _secret });

    return token;
}

int AuthService::CheckTokenAndParseUserId(const std::string &token) {
    try {
        // 解码并验证token
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs512{_secret})
            .with_issuer(_jwtIssuer);

        verifier.verify(decoded);

        // 获取用户ID
        auto userIdClaim = decoded.get_payload_claim("id");
        std::string userIdStr = userIdClaim.as_string();
        int32_t userId = std::stoi(userIdStr);

        return userId;
    }
    catch (const std::exception &e) {
        LOG_ERROR << std::format("对于Token: {}, 验证失败: {}", token, e.what());
        LOG_ERROR << "Token验证失败: " << e.what();
        return -1;
    }
}

Task<HttpResult> AuthService::RegisterByEmail(
    const std::string &username, 
    const std::string &password, 
    const std::string &email, 
    const std::string &verifyCode,
    const UserPrivileges &privilege, 
    const bool &isMale, 
    const std::string &nickname) {
    // 依赖
    auto _mfaService = MFAService::Instance();
    auto _gitlabService = GitlabService::Instance();

    // 
    HttpResult result;

    // 检查验证码
    auto [isSuccess, errMsg] = co_await _mfaService->VerifyTheCode(email, verifyCode, eMFAType::Register);
    if(!isSuccess) {
        result.setResult(-1, errMsg);
        co_return result;
    }

    // 数据库相关初始化
    auto dbClientPtr = drogon::app().getDbClient();
    drogon::orm::Mapper<User> mapperUsers(dbClientPtr);
    drogon::orm::Mapper<UserGitlabInfo> mapperUserGitlabInfos(dbClientPtr);

    // 检查用户名是否已存在
    User foundUser;
    bool isFound;
    try {
        foundUser = mapperUsers.findFutureOne(Criteria(User::Cols::_name, CompareOperator::EQ, username)).get();
        isFound = true;
    }
    catch (const drogon::orm::DrogonDbException& e) {
        // 有异常说明没找到
        isFound = false;
    }
    if (isFound) {
        LOG_ERROR << "用户名已存在";
        result.setResult(-1, "用户名已存在");
        co_return result;
    }

    // 检查邮箱是否已存在
    try{
        foundUser = mapperUsers.findFutureOne(Criteria(User::Cols::_email, CompareOperator::EQ, email)).get();
        isFound = true;
    }
    catch (const drogon::orm::DrogonDbException& e) {
        isFound = false;
    }
    if (isFound) {
        LOG_ERROR << std::format("邮箱: {} 已存在", email);
        result.setResult(-1, "邮箱已存在");
        co_return result;
    }

    // 创建新用户
    auto newUser = User();
    newUser.setName(username);
    auto [passwordHash, passwordSalt] = CreatePasswordHash(password);
    newUser.setPasswordHash(vectorToString(passwordHash));
    newUser.setPasswordSalt(vectorToString(passwordSalt));
    newUser.setPrivilege(int(privilege));
    newUser.setNickName(nickname.empty() ? username : nickname);
    newUser.setEmail(email);
    newUser.setIsMale(isMale);
    newUser.setCreateAt(trantor::Date::now());

    // 同步创建 GitLab 账号
    int gitlabUserId = 0;
    if(!_gitlabService->createUser(username, password, email, gitlabUserId)){
        result.setResult(-1, "创建 GitLab 账号失败");
        co_return result;
    }

    // 邀请用户加入项目
    if(!_gitlabService->adminInvitationProject(1, UEAdminAPI::GitlabService::AccessLevels::Developer, gitlabUserId)){
        result.setResult(-1, "邀请用户加入项目失败");
        co_return result;
    }

    // 创建gitlab impersonation token
    uint32_t gitImpersonationTokenId = 0;
    string gitlabImpersonationToken = "";
    if(!_gitlabService->createImpersonationToken(gitlabUserId, gitlabImpersonationToken, gitImpersonationTokenId)){
        result.setResult(-1, "创建 GitLab Impersonation Token失败");
        co_return result;
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
        co_return result;
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
        co_return result;
    }
    
    result.setResult(0, "创建用户成功");
    result.jsondata["userId"] = *insertedUser.getId();

    co_return result;
}


Task<HttpResult> AuthService::RegisterByPhone(
        const std::string &username, 
        const std::string &password, 
        const std::string &phoneNumber, 
        const std::string &verifyCode, 
        const UserPrivileges &privilege, 
        const bool &isMale, 
        const std::string &nickname) {
    // 依赖
    auto _mfaService = MFAService::Instance();
    auto _gitlabService = GitlabService::Instance();

    // 
    HttpResult result;

    // 检查验证码
    auto [isSuccess, errMsg] = co_await _mfaService->VerifyTheCode(phoneNumber, verifyCode, eMFAType::Register);
    if(!isSuccess) {
        result.setResult(-1, errMsg);
        co_return result;
    }

    // 数据库相关初始化
    auto dbClientPtr = drogon::app().getDbClient();
    drogon::orm::Mapper<User> mapperUsers(dbClientPtr);
    drogon::orm::Mapper<UserGitlabInfo> mapperUserGitlabInfos(dbClientPtr);

    // 检查用户名是否已存在
    User foundUser;
    bool isFound;
    try {
        foundUser = mapperUsers.findFutureOne(Criteria(User::Cols::_name, CompareOperator::EQ, username)).get();
        isFound = true;
    }
    catch (const drogon::orm::DrogonDbException& e) {
        // 有异常说明没找到
        isFound = false;
    }
    if (isFound) {
        LOG_ERROR << "用户名已存在";
        result.setResult(-1, "用户名已存在");
        co_return result;
    }

    // 检查邮箱是否已存在
    try{
        foundUser = mapperUsers.findFutureOne(Criteria(User::Cols::_telephone_number, CompareOperator::EQ, phoneNumber)).get();
        isFound = true;
    }
    catch (const drogon::orm::DrogonDbException& e) {
        isFound = false;
    }
    if (isFound) {
        LOG_ERROR << std::format("邮箱: {} 已存在", phoneNumber);
        result.setResult(-1, "邮箱已存在");
        co_return result;
    }

    // 创建新用户
    auto newUser = User();
    newUser.setName(username);
    auto [passwordHash, passwordSalt] = CreatePasswordHash(password);
    newUser.setPasswordHash(vectorToString(passwordHash));
    newUser.setPasswordSalt(vectorToString(passwordSalt));
    newUser.setPrivilege(int(privilege));
    newUser.setNickName(nickname.empty() ? username : nickname);
    newUser.setTelephoneNumber(phoneNumber);
    newUser.setIsMale(isMale);
    newUser.setCreateAt(trantor::Date::now());

    // 同步创建 GitLab 账号
    int gitlabUserId = 0;
    // 生成fake email
    std::string fakeEmail = std::format("{}@fake.com", username);
    if(!_gitlabService->createUser(username, password, fakeEmail, gitlabUserId)){
        result.setResult(-1, "创建 GitLab 账号失败");
        co_return result;
    }

    // 邀请用户加入项目
    if(!_gitlabService->adminInvitationProject(1, UEAdminAPI::GitlabService::AccessLevels::Developer, gitlabUserId)){
        result.setResult(-1, "邀请用户加入项目失败");
        co_return result;
    }

    // 创建gitlab impersonation token
    uint32_t gitImpersonationTokenId = 0;
    string gitlabImpersonationToken = "";
    if(!_gitlabService->createImpersonationToken(gitlabUserId, gitlabImpersonationToken, gitImpersonationTokenId)){
        result.setResult(-1, "创建 GitLab Impersonation Token失败");
        co_return result;
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
        co_return result;
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
        co_return result;
    }
    
    result.setResult(0, "创建用户成功");
    result.jsondata["userId"] = *insertedUser.getId();

    co_return result;
}

drogon::Task<UEAdminAPI::utils::HttpResult> AuthService::Register(drogon_model::UEAdminAPI::User &user) {
    throw std::runtime_error("Not implemented");
}

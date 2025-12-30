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
#include "utils/MFA/MFA_Channels.h"
#include "utils/MFA/MFACodePair.h"
#include "services/ThirdPartyLoginService.h"

#include "models/UserFlashtoken.h"
#include "models/User.h"
#include "models/UserGitlabInfo.h"
#include "models/UserThirdPartyInfo.h"
#include "models/ThirdpartyPlatforms.h"

#include <utils/EnumUserPrivileges.h>
#include <utils/RandomGenerator.h>
#include <utils/DataFormatUtils.h>
#include <numeric>
#include <utils/PostParamMap.h>

using namespace drogon_model::UEAdminAPI;
using namespace drogon::orm;
using namespace UEAdminAPI;
using namespace UEAdminAPI::Services;
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

drogon::Task<std::tuple<std::string, std::string, int>> AuthService::NewTokenPair(int userId) {
    auto dbClientPtr = drogon::app().getDbClient();
    Mapper<UserFlashtoken> mapper(dbClientPtr);

    int status = RandomGenerator::getInt(1, INT_MAX);

    UserFlashtoken row;
    bool needInsert = false;
    try {
        row = mapper.findByPrimaryKey(userId);
    } catch (const UnexpectedRows &e) {
        needInsert = true;
    }

    std::string flashToken = CreateFlashToken(userId, status);
    std::string token = CreateToken(userId, status);

    if (needInsert) {
        UserFlashtoken newRow;
        newRow.setUserId(userId);
        newRow.setStatus(status);
        newRow.setStatusForToken(status);
        mapper.insert(newRow);
    } else {
        row.setStatus(status);
        row.setStatusForToken(status);
        auto ret = mapper.update(row);
        if (ret != 1) {
            co_return std::make_tuple(std::string(), std::string(), -1);
        }
    }

    co_return std::make_tuple(token, flashToken, status);
}

drogon::Task<std::tuple<std::string, int>> AuthService::NewToken(int userId, int status) {
    if(status == -1){
        status = RandomGenerator::getInt(1, INT_MAX);
    }

    //auto dbClientPtr = drogon::app().getDbClient();
    //Mapper<UserFlashtoken> mapper(dbClientPtr);

    //UserFlashtoken row;
    //bool needInsert = false;
    //try {
    //    row = mapper.findByPrimaryKey(userId);
    //} catch (const UnexpectedRows &e) {
    //    needInsert = true;
    //}

    //if (needInsert) {
    //    UserFlashtoken newRow;
    //    newRow.setUserId(userId);
    //    newRow.setStatusForToken(status);
    //    mapper.insert(newRow);
    //} else {
    //    row.setStatusForToken(status);
    //    (void)mapper.update(row);
    //}

    std::string token = CreateToken(userId, status);
    co_return std::make_tuple(token, status);
}

std::tuple<bool, int, int, int> AuthService::CheckTokenAndParseUserId(const std::string &token) {
    std::string tokenType;
    int status;
    int32_t userId;
    int isFlashToken;
    try{
        // 解码并验证token
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs512{_secret})
            .with_issuer(_jwtIssuer);
        tokenType = decoded.get_payload_claim("tokenType").as_string();
        if(tokenType != "token" && tokenType != "flashToken"){
            LOG_ERROR << std::format("对于Token: {}, tokenType 既不是 token, 也不是 flashToken", token);
            return std::make_tuple(false, -1, -1, -1);
        }
        isFlashToken = tokenType == "flashToken";
        status = std::stoi(decoded.get_payload_claim("status").as_string());
        userId = std::stoi(decoded.get_payload_claim("id").as_string());
        return std::make_tuple(true, userId, status, isFlashToken);
    }
    catch (const std::exception &e) {
        LOG_ERROR << std::format("对于Token: {}, 有缺失的 Claim: {}", token, e.what());
        return std::make_tuple(false, -1, -1, -1);
    }
    // token格式不正确
    catch (const std::invalid_argument &e) {
        LOG_ERROR << std::format("对于Token: {}, 格式错误: {}", token, e.what());
        return std::make_tuple(false, -1, -1, -1);
    }
}

Task<HttpResult> AuthService::VerifyToken(const std::string &token) {
    HttpResult result;
    if (!DataFormatUtil::isJwtString(token)) {
        result.setResult(1, "invalid token");
        co_return result;
    }

    auto [isSuccess, userId, status, isFlashToken] = CheckTokenAndParseUserId(token);
    bool valid = true;
    // 只有FlashToken才检查状态
    if(isFlashToken){
        valid = co_await CheckTokenStatus(userId, status, isFlashToken);
    }

    result.jsondata["valid"] = valid;
    result.jsondata["userId"] = userId;
    result.jsondata["status"] = status;
    result.jsondata["tokenType"] = isFlashToken == -1 ? "unset" : isFlashToken == 1 ? "flashToken" : "token";

    if (!isSuccess) {
        result.setResult(-1, std::format("{}验证失败", result.jsondata["tokenType"].asString()));
        co_return result;
    }
    if(!valid){
        result.setResult(-1, std::format("{}已失效", result.jsondata["tokenType"].asString()));
        co_return result;
    }

    result.setResult(0, "Token验证成功");
    co_return result;
}

Task<HttpResult> AuthService::RegisterByEmail(
    const std::string &username, 
    const std::string &password, 
    const std::string &email, 
    const std::string &code,
    const UserPrivileges &privilege, 
    const bool &isMale, 
    const std::string &nickname) {
    HttpResult result;
    auto _mfaService = MFAService::Instance();
    // 1. 检查验证码 (这里可以用 co_await 是因为 VerifyTheCode 返回的是 Task 或者 Awaitable)
    auto [isSuccess, errMsg] = co_await _mfaService->VerifyTheCode(email, code, eMFAType::Register);
    if(!isSuccess) {
        result.setResult(-1, errMsg);
        co_return result;
    }
    // 2. 数据库查重
    auto dbClientPtr = drogon::app().getDbClient();
    drogon::orm::Mapper<User> mapperUsers(dbClientPtr);
    // 使用同步辅助函数
    if (co_await CheckIfExist(mapperUsers, Criteria(User::Cols::_name, CompareOperator::EQ, username))) {
        result.setResult(-1, "用户名已存在");
        co_return result;
    }
    if (co_await CheckIfExist(mapperUsers, Criteria(User::Cols::_email, CompareOperator::EQ, email))) {
        result.setResult(-1, "邮箱已存在");
        co_return result;
    }
    // 3. 组装对象
    auto newUser = User();
    newUser.setName(username);
    auto [passwordHash, passwordSalt] = CreateStrPasswordHash(password);
    newUser.setPasswordHash(passwordHash);
    newUser.setPasswordSalt(passwordSalt);
    newUser.setPrivilege(int(privilege));
    newUser.setNickName(nickname.empty() ? username : nickname);
    newUser.setIsMale(isMale);
    newUser.setCreateAt(trantor::Date::now());
    newUser.setEmail(email);
    // 4. 执行
    co_return co_await ExecuteRegistrationTransaction(newUser, password, email);
}


Task<HttpResult> AuthService::RegisterByPhone(
        const std::string &username, 
        const std::string &password, 
        const std::string &phoneNumber, 
        const std::string &code, 
        const UserPrivileges &privilege, 
        const bool &isMale, 
        const std::string &nickname) {
    // 依赖
    auto _mfaService = MFAService::Instance();
    
    HttpResult result;

    // 检查验证码
    auto [isSuccess, errMsg] = co_await _mfaService->VerifyTheCode(phoneNumber, code, eMFAType::Register);
    if(!isSuccess) {
        result.setResult(-1, errMsg);
        co_return result;
    }
    // 数据库初始化
    auto dbClientPtr = drogon::app().getDbClient();
    drogon::orm::Mapper<User> mapperUsers(dbClientPtr);
    // 查重
    if (co_await CheckIfExist(mapperUsers, Criteria(User::Cols::_name, CompareOperator::EQ, username))) {
        result.setResult(-1, "用户名已存在");
        co_return result;
    }
    if (co_await CheckIfExist(mapperUsers, Criteria(User::Cols::_telephone_number, CompareOperator::EQ, phoneNumber))) {
        result.setResult(-1, "手机号已存在");
        co_return result;
    }
    // 构建用户
    auto newUser = User();
    newUser.setName(username);
    auto [passwordHash, passwordSalt] = CreateStrPasswordHash(password);
    newUser.setPasswordHash(passwordHash);
    newUser.setPasswordSalt(passwordSalt);
    newUser.setPrivilege(int(privilege));
    newUser.setNickName(nickname.empty() ? username : nickname);
    newUser.setIsMale(isMale);
    newUser.setCreateAt(trantor::Date::now());
    newUser.setTelephoneNumber(phoneNumber);
    std::string fakeEmail = std::format("{}@fake.com", username);
    // 执行
    co_return co_await ExecuteRegistrationTransaction(newUser, password, fakeEmail);
}

drogon::Task<UEAdminAPI::utils::HttpResult> AuthService::RegisterWithThirdPartyByEmail(
    const std::string &username, 
    const std::string &password, 
    const std::string &email, 
    const std::string &verifyCode, 
    const std::string &third_platform_name, 
    const std::string &third_code, 
    const std::string &third_verifyCode, 
    const UserPrivileges &privilege, 
    const bool &isMale, 
    const std::string &nickname) 
{
    // 依赖
    auto _thirdPartyLoginService = ThirdPartyLoginService::Instance();
    auto _mfaService = MFAService::Instance();

    HttpResult result;
    // 1. 检查验证码 (这里可以用 co_await 是因为 VerifyTheCode 返回的是 Task 或者 Awaitable)
    auto [isSuccess, errMsg] = co_await _mfaService->VerifyTheCode(email, verifyCode, eMFAType::Register);
    if(!isSuccess) {
        result.setResult(-1, errMsg);
        co_return result;
    }
    // 2. 数据库查重
    auto dbClientPtr = drogon::app().getDbClient();
    drogon::orm::Mapper<User> mapperUsers(dbClientPtr);
    // 使用同步辅助函数
    if (co_await CheckIfExist(mapperUsers, Criteria(User::Cols::_name, CompareOperator::EQ, username))) {
        result.setResult(-1, "用户名已存在");
        co_return result;
    }
    if (co_await CheckIfExist(mapperUsers, Criteria(User::Cols::_email, CompareOperator::EQ, email))) {
        result.setResult(-1, "邮箱已存在");
        co_return result;
    }
    // 3. 检查第三方平台验证码
    result = co_await _thirdPartyLoginService->VerifyLogin(third_platform_name, third_code, third_verifyCode);
    if(result.code != 0) {
        co_return result;
    }
    // 4. 检查是否已绑定
    if(result.jsondata["allready_bind"].asBool()) {
        result.setResult(-1, "该邮箱已绑定过账号");
        co_return result;
    }
    // 4. 组装对象
    auto newUser = User();
    newUser.setName(username);
    auto [passwordHash, passwordSalt] = CreateStrPasswordHash(password);
    newUser.setPasswordHash(passwordHash);
    newUser.setPasswordSalt(passwordSalt);
    newUser.setPrivilege(int(privilege));
    newUser.setNickName(nickname.empty() ? username : nickname);
    newUser.setIsMale(isMale);
    newUser.setCreateAt(trantor::Date::now());
    newUser.setEmail(email);
    // 5. 执行
    result = co_await ExecuteRegistrationTransaction(newUser, password, email);
    if(result.code != 0) {
        co_return result;
    }
    // 6. 构造第三方账号
    auto newThirdPartyInfo = UserThirdPartyInfo();
    auto [platform, loginValue] = co_await _thirdPartyLoginService->getCodeAndItsPlatform(third_code);
    newThirdPartyInfo.setUserId(newUser.getValueOfId());
    newThirdPartyInfo.setPlatformId(int(platform));
    newThirdPartyInfo.setOpenId(loginValue->openId);
    newThirdPartyInfo.setAccessToken(loginValue->accessToken);
    newThirdPartyInfo.setNickName(loginValue->nickName);
    newThirdPartyInfo.setAvatarImgUrl(loginValue->avatarImgUrl);
    // 7. 插入
    auto mapperThirdPartyInfo = drogon::orm::Mapper<UserThirdPartyInfo>(dbClientPtr);
    try{
        mapperThirdPartyInfo.insert(newThirdPartyInfo);
    }catch (const drogon::orm::UnexpectedRows &e) {
        result.setResult(-1, "插入第三方账号失败, 但账号已创建, 可之后再绑定");
        result.jsondata["userCreatedButThirdPartyNotBind"] = true;
        co_return result;
    }
    
    co_return result;
}

drogon::Task<UEAdminAPI::utils::HttpResult> AuthService::RegisterWithThirdPartyByPhone(
    const std::string &username, 
    const std::string &password, 
    const std::string &phoneNumber, 
    const std::string &verifyCode, 
    const std::string &third_platform_name, 
    const std::string &third_code, 
    const std::string &third_verifyCode, 
    const UserPrivileges &privilege, 
    const bool &isMale, 
    const std::string &nickname) 
{
    // 依赖
    auto _thirdPartyLoginService = ThirdPartyLoginService::Instance();
    auto _mfaService = MFAService::Instance();

    HttpResult result;
    // 1. 检查验证码
    auto [isSuccess, errMsg] = co_await _mfaService->VerifyTheCode(phoneNumber, verifyCode, eMFAType::Register);
    if(!isSuccess) {
        result.setResult(-1, errMsg);
        co_return result;
    }
    // 2. 数据库查重
    auto dbClientPtr = drogon::app().getDbClient();
    drogon::orm::Mapper<User> mapperUsers(dbClientPtr);
    // 使用同步辅助函数
    if (co_await CheckIfExist(mapperUsers, Criteria(User::Cols::_name, CompareOperator::EQ, username))) {
        result.setResult(-1, "用户名已存在");
        co_return result;
    }
    if (co_await CheckIfExist(mapperUsers, Criteria(User::Cols::_telephone_number, CompareOperator::EQ, phoneNumber))) {
        result.setResult(-1, "手机号已存在");
        co_return result;
    }
    // 3. 检查第三方平台验证码
    result = co_await _thirdPartyLoginService->VerifyLogin(third_platform_name, third_code, third_verifyCode);
    if(result.code != 0) {
        co_return result;
    }
    // 4. 检查是否已绑定
    if(result.jsondata["allready_bind"].asBool()) {
        result.setResult(-1, "该第三方账号已绑定过账号");
        co_return result;
    }
    // 4. 组装对象
    auto newUser = User();
    newUser.setName(username);
    auto [passwordHash, passwordSalt] = CreateStrPasswordHash(password);
    newUser.setPasswordHash(passwordHash);
    newUser.setPasswordSalt(passwordSalt);
    newUser.setPrivilege(int(privilege));
    newUser.setNickName(nickname.empty() ? username : nickname);
    newUser.setIsMale(isMale);
    newUser.setCreateAt(trantor::Date::now());
    newUser.setTelephoneNumber(phoneNumber);
    std::string fakeEmail = std::format("{}@fake.com", username);

    // 5. 执行
    result = co_await ExecuteRegistrationTransaction(newUser, password, fakeEmail);
    if(result.code != 0) {
        co_return result;
    }
    // 6. 构造第三方账号
    auto newThirdPartyInfo = UserThirdPartyInfo();
    auto [platform, loginValue] = co_await _thirdPartyLoginService->getCodeAndItsPlatform(third_code);
    newThirdPartyInfo.setUserId(newUser.getValueOfId());
    newThirdPartyInfo.setPlatformId(int(platform));
    newThirdPartyInfo.setOpenId(loginValue->openId);
    newThirdPartyInfo.setAccessToken(loginValue->accessToken);
    newThirdPartyInfo.setNickName(loginValue->nickName);
    newThirdPartyInfo.setAvatarImgUrl(loginValue->avatarImgUrl);
    // 7. 插入
    auto mapperThirdPartyInfo = drogon::orm::Mapper<UserThirdPartyInfo>(dbClientPtr);
    try{
        mapperThirdPartyInfo.insert(newThirdPartyInfo);
    }catch (const drogon::orm::UnexpectedRows &e) {
        result.setResult(-1, "插入第三方账号失败, 但账号已创建, 可之后再绑定");
        result.jsondata["userCreatedButThirdPartyNotBind"] = true;
        co_return result;
    }
    
    co_return result;
}

drogon::Task<bool> AuthService::CheckTokenStatus(const int &userId, const int &status, const bool &isFlashToken) {
    auto dbClientPtr = drogon::app().getDbClient();
    Mapper<UserFlashtoken> mapperTokens(dbClientPtr);

    bool hasException = false;
    UserFlashtoken token;
    try{
        token = mapperTokens.findByPrimaryKey(userId);
    }catch (const UnexpectedRows &e) {
        LOG_ERROR << "查询用户token失败:" <<e.what();
        hasException = true;
    }

    if(hasException)
        co_return false;

    if (isFlashToken) {
        if (token.getValueOfStatus() != status)
            co_return false;
    }
    else {
        if (token.getValueOfStatusForToken() != status)
            co_return false;
    }

    co_return true;
}

drogon::Task<UEAdminAPI::utils::HttpResult> AuthService::LoginByPwd(const std::string &userName, const std::string &pwd)
{
    HttpResult result;

    if (userName.empty() || pwd.empty()) {
        result.setResult(-1, "缺少用户名或密码");
        co_return result;
    }

    auto dbClientPtr = drogon::app().getDbClient();

    Mapper<User> mapperUsers(dbClientPtr);

    User targetUser;
    try{
        targetUser = mapperUsers.findFutureOne(Criteria(User::Cols::_name, CompareOperator::EQ, userName)).get();
    }catch (const UnexpectedRows &e) {
        LOG_ERROR << "查询用户失败:" << e.what();
        result.setResult(-1, "用户名或密码错误");
        co_return result;
    }

    auto hash = targetUser.getPasswordHash();
    auto salt = targetUser.getPasswordSalt();
    if(!hash || !salt){
        result.setResult(-1, "用户没有设置密码");
        co_return result;
    }

    if(!VerifyPasswordHash(pwd, hash, salt)){
        result.setResult(-1, "用户名或密码错误");
        co_return result;
    }

    auto [token, flashToken, status] = co_await NewTokenPair(targetUser.getValueOfId());
    if (status == -1) {
        result.setResult(-1, "更新状态失败");
        co_return result;
    }

    result.jsondata["flashToken"] = flashToken;
    result.jsondata["token"] = token;
    result.jsondata["id"] = targetUser.getValueOfId();
    result.setResult(0, "登录成功");

    co_return result;
}

drogon::Task<UEAdminAPI::utils::HttpResult> AuthService::LoginByOther(const std::string &target, const std::string &targetDBColName, const std::string &code) {
    // 依赖
    auto _mfaService = MFAService::Instance();

    HttpResult result;

    // 检查验证码
    auto [isSuccess, errMsg] = co_await _mfaService->VerifyTheCode(target, code, eMFAType::Login);
    if(!isSuccess) {
        LOG_ERROR << errMsg;
        result.setResult(-1, "验证码错误");
        co_return result;
    }

    auto dbClientPtr = drogon::app().getDbClient();
    drogon::orm::Mapper<User> mapperUsers(dbClientPtr);

    // 查找用户
    User targetUser;
    try{
        targetUser = mapperUsers.findFutureOne(Criteria(targetDBColName, CompareOperator::EQ, target)).get();
    }
    // 没找到会抛出异常
    catch (UnexpectedRows& e){
        LOG_ERROR << "邮箱验证码登录查询用户时出错!";
        result.setResult(-1, "用户不存在");
        co_return result;
    }

    auto [token2, flashToken2, status2] = co_await NewTokenPair(targetUser.getValueOfId());
    if (status2 == -1) {
        result.setResult(-1, "更新状态失败");
        co_return result;
    }

    result.jsondata["flashToken"] = flashToken2;
    result.jsondata["token"] = token2;
    result.jsondata["id"] = targetUser.getValueOfId();
    result.setResult(0, "登录成功");

    co_return result;
}

drogon::Task<UEAdminAPI::utils::HttpResult> AuthService::LoginByUserId(int userId) {
    HttpResult result;
    
    auto dbClientPtr = drogon::app().getDbClient();
    drogon::orm::Mapper<User> mapperUsers(dbClientPtr);
    drogon::orm::Mapper<UserFlashtoken> mapperFlashToken(dbClientPtr);

    bool isUserExist = false;
    User targetUser;
    try {
        targetUser = mapperUsers.findByPrimaryKey(userId);
        isUserExist = true;
    } catch (const UnexpectedRows& e) {
        result.setResult(-1, "用户不存在");
    }
    if(!isUserExist){
        co_return result;
    }

    auto [token, flashToken, status] = co_await NewTokenPair(targetUser.getValueOfId());
    if (status == -1) {
        result.setResult(-1, "更新状态失败");
        co_return result;
    }

    result.jsondata["flashToken"] = flashToken;
    result.jsondata["token"] = token;
    result.jsondata["id"] = targetUser.getValueOfId();
    result.setResult(0, "登录成功");

    co_return result;
}

drogon::Task<UEAdminAPI::utils::HttpResult> AuthService::LoginByEmail(const std::string &email, const std::string &code){
    co_return co_await LoginByOther(email, User::Cols::_email, code);
}

drogon::Task<UEAdminAPI::utils::HttpResult> AuthService::LoginByPhone(const std::string &phone, const std::string &code) {
    co_return co_await LoginByOther(phone, User::Cols::_telephone_number, code);
}

drogon::Task<UEAdminAPI::utils::HttpResult> AuthService::LoginByFlashToken(const std::string &flashToken) {
    HttpResult result;

    auto [isSuccess, userId, status, isFlashToken] = CheckTokenAndParseUserId(flashToken);
    if(isFlashToken != 1){
        result.setResult(-1, "不是FlashToken");
        co_return result;
    }
    if(!isSuccess || userId == -1){
        result.setResult(-1, "FlashToken验证失败");
        co_return result;
    }

    bool valid = co_await CheckTokenStatus(userId, status, true);
    if(!valid){
        result.setResult(-1, "FlashToken已失效");
        co_return result;
    }

    auto [token, _] = co_await NewToken(userId);
    result.jsondata["token"] = token;
    result.jsondata["flashToken"] = flashToken;
    result.jsondata["id"] = userId;
    result.setResult(0, "刷新成功");
    co_return result;
}

drogon::Task<UEAdminAPI::utils::HttpResult> AuthService::GetSelfInfo(const std::string &token) {
    HttpResult result;

    if(token.empty()){
        result.setResult(-1, "缺少Token");
        co_return result;
    }

    result = co_await VerifyToken(token);
    if(result.jsondata["tokenType"].asString() != "token"){
        result.setResult(-1, "不是token");
        co_return result;
    }
    if(result.code != 0){
        result.setResult(-1, "Token已失效");
        co_return result;
    }
    int userId = result.jsondata["userId"].asInt();

    auto dbClientPtr = drogon::app().getDbClient();
    Mapper<User> mapperUser(dbClientPtr);

    User user;
    try {
        user = mapperUser.findByPrimaryKey(userId);
    } catch (const UnexpectedRows &e) {
        result.setResult(-1, "数据库错误");
        co_return result;
    }

    result.jsondata["id"] = user.getValueOfId();
    result.jsondata["userName"] = user.getValueOfName();
    result.jsondata["userNick"] = user.getValueOfNickName();
    if(user.getTelephoneNumber()){
        result.jsondata["tel"] = user.getValueOfTelephoneNumber();
    } else {
        result.jsondata["tel"] = "";
    }
    result.jsondata["created_at"] = user.getValueOfCreateAt().toFormattedStringLocal(false);
    if(user.getIsMale()){
        result.jsondata["sex"] = user.getValueOfIsMale() ? "man" : "woman";
    } else {
        result.jsondata["sex"] = Json::nullValue;
    }

    result.jsondata["configured_third_platform_name"] = Json::Value(Json::arrayValue);
    try {
        auto pairs = user.getThird_party_platforms(dbClientPtr);
        for(const auto &p : pairs){
            const auto &platform = p.first;
            const auto &info = p.second;
            Json::Value temp(Json::objectValue);
            temp["third_platform_name"] = platform.getValueOfPlatformName();
            temp["nickName"] = info.getValueOfNickName();
            temp["headImageUrl"] = info.getValueOfAvatarImgUrl();
            result.jsondata["configured_third_platform_name"].append(temp);
        }
    } catch (...) {
    }

    std::string gitlabToken = "";
    try {
        auto gitInfo = user.getUser_gitlab_info(dbClientPtr);
        gitlabToken = gitInfo.getValueOfGitlabImpersonationToken();
    } catch (...) {
    }
    result.jsondata["gitlab_token"] = gitlabToken;

    result.jsondata["email"] = Json::Value(Json::arrayValue);
    if(user.getEmail()){
        result.jsondata["email"].append(user.getValueOfEmail());
    }

    result.setResult(0, "success");
    co_return result;
}

Task<HttpResult> AuthService::ExecuteRegistrationTransaction(
    User preparedUser, 
    const std::string &rawPassword, 
    const std::string &gitlabEmail) {
    
    auto _gitlabService = GitlabService::Instance();
    HttpResult result;
    // 1. 同步创建 GitLab 账号
    int gitlabUserId = 0;
    if(!_gitlabService->createUser(preparedUser.getValueOfName(), rawPassword, gitlabEmail, gitlabUserId)){
        result.setResult(-1, "创建 GitLab 账号失败");
        co_return result;
    }
    // 2. 邀请用户加入项目
    if(!_gitlabService->adminInvitationProject(1, UEAdminAPI::GitlabService::AccessLevels::Developer, gitlabUserId)){
        // 注意：这里如果失败，理论上也应该回滚删除刚创建的 Gitlab 用户，原代码未处理，这里建议加上
        _gitlabService->deleteUser(gitlabUserId); 
        result.setResult(-1, "邀请用户加入项目失败");
        co_return result;
    }
    // 3. 创建 gitlab impersonation token
    uint32_t gitImpersonationTokenId = 0;
    std::string gitlabImpersonationToken = "";
    if(!_gitlabService->createImpersonationToken(gitlabUserId, gitlabImpersonationToken, gitImpersonationTokenId)){
        _gitlabService->deleteUser(gitlabUserId);
        result.setResult(-1, "创建 GitLab Impersonation Token失败");
        co_return result;
    }
    // 4. 数据库操作准备
    auto dbClientPtr = drogon::app().getDbClient();
    drogon::orm::Mapper<User> mapperUsers(dbClientPtr);
    drogon::orm::Mapper<UserGitlabInfo> mapperUserGitlabInfos(dbClientPtr);
    // 5. 插入用户 (Step 1)
    bool step1Failed = false;
    try {
        // insertFuture 是阻塞等待结果的 future，原代码使用了 .get()
        // 既然在协程中，建议使用 co_await mapperUsers.insertFuture(preparedUser); 
        // 但为了保持你原代码逻辑的一致性，这里保留 .get() 风格或转为 co_await
        mapperUsers.insert(preparedUser);
    } catch (const drogon::orm::DrogonDbException& e) {
        LOG_ERROR << "插入用户失败:" << e.base().what();
        step1Failed = true;
    }
    // 6. 插入 Gitlab Info (Step 2)
    bool step2Failed = false;
    UserGitlabInfo newUserGitlab;
    if (!step1Failed) {
        newUserGitlab = UserGitlabInfo();
        newUserGitlab.setUserId(preparedUser.getValueOfId());
        newUserGitlab.setGitId(gitlabUserId);
        newUserGitlab.setGitlabImpersonationToken(gitlabImpersonationToken);
        newUserGitlab.setGitlabImpersonationTokenId(gitImpersonationTokenId);
        
        try {
            mapperUserGitlabInfos.insert(newUserGitlab);
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "插入Gitlab信息失败:" << e.base().what();
            step2Failed = true;
        }
    }
    // 7. 回滚逻辑 (Rollback)
    if (step1Failed || step2Failed) {
        // 回滚 Gitlab 用户
        if (!_gitlabService->deleteUser(gitlabUserId)) {
            LOG_ERROR << "创建用户失败, 回滚时删除Gitlab用户失败";
        } else {
            LOG_ERROR << "创建用户失败, 回滚, 先前创建的Gitlab用户已成功删除";
        }
        // 如果是 Step 2 失败，说明 User 已经插进去了，需要把 User 删掉
        if (!step1Failed && step2Failed) {
            try {
                mapperUsers.deleteOne(preparedUser);
                LOG_ERROR << "创建用户失败, 回滚时删除先前插入的用户数据成功";
            } catch (...) {
                LOG_ERROR << "创建用户失败, 回滚时删除先前插入的用户数据失败";
            }
        }
        result.setResult(-1, "创建用户失败");
        co_return result;
    }
    
    // 成功
    result.setResult(0, "创建用户成功");
    result.jsondata["userId"] = preparedUser.getValueOfId();
    co_return result;
}

Task<bool> AuthService::CheckIfExist(drogon::orm::Mapper<User> &mapper, const drogon::orm::Criteria &criteria) {
    try {
        // 阻塞式调用
        mapper.findOne(criteria);
        co_return true;
    } catch (...) {
        co_return false;
    }
}

drogon::Task<UEAdminAPI::utils::HttpResult> AuthService::UpdateUserInfo(
    const std::string &token,
    const UEAdminAPI::PostParamMap &pm)
{
    auto _mfaService = MFAService::Instance();
    UEAdminAPI::utils::HttpResult result;

    result = co_await VerifyToken(token);
    if(result.jsondata["tokenType"].asString() != "token"){
        result.setResult(-1, "不是token");
        co_return result;
    }
    if(result.code != 0){
        result.setResult(-1, "Token已失效");
        co_return result;
    }
    int userId = result.jsondata["userId"].asInt();

    auto [mfaOk, mfaErr] = co_await VerifyUserTargetMFA(pm.getParam("target"), pm.getParam("verifyCode"), userId, eMFAType::ModifyUser);
    if (!mfaOk) {
        result.setResult(-1, mfaErr.empty() ? std::string("验证码错误") : mfaErr);
        co_return result;
    }

    bool isModified = false;
    auto dbClientPtr = drogon::app().getDbClient();
    Mapper<User> mapperUser(dbClientPtr);
    Mapper<UserFlashtoken> mapperUserFlashtoken(dbClientPtr);
    User user;
    try {
        user = mapperUser.findByPrimaryKey(userId);
    } catch (const UnexpectedRows &) {
        result.setResult(-1, "用户不存在");
        co_return result;
    }

    if (pm.hasParam("email") && pm.hasParam("tel")) {
        isModified = true;
        result.setResult(-1, "电话或邮箱不能同时修改");
        co_return result;
    }
    bool isEmailOrPhoneModified = pm.hasParam("email");
    if (pm.hasParam("email") || pm.hasParam("tel")) {
        isModified = true;
        auto [mfaOk, mfaErr] = co_await _mfaService->VerifyTheCode(isEmailOrPhoneModified ? pm.getParam("email") : pm.getParam("tel"), pm.getParam("newEmailOrPhoneVerifyCode"), isEmailOrPhoneModified ? eMFAType::EmailBind : eMFAType::PhoneChange);
        if (!mfaOk) {
            result.setResult(-1, mfaErr.empty() ? std::string("新邮箱或电话验证码错误") : mfaErr);
            co_return result;
        }
        if(isEmailOrPhoneModified) {
            user.setEmail(pm.getParam("email"));
        } else {
            user.setTelephoneNumber(pm.getParam("tel"));
        }
    }

    if (pm.hasParam("username")) {
        isModified = true;
        const auto newName = pm.getParam("username");
        if (!DataFormatUtil::checkUserName(newName)) {
            result.setResult(-1, "用户名格式不合法");
            co_return result;
        }
        try {
            mapperUser.findOne(Criteria(User::Cols::_name, CompareOperator::EQ, newName) &&
                               Criteria(User::Cols::_id, CompareOperator::NE, userId));
            result.setResult(-1, "用户名已存在");
            co_return result;
        } catch (...) {}
        user.setName(newName);
    }

    if (pm.hasParam("nickname")) {
        isModified = true;
        user.setNickName(pm.getParam("nickname"));
    }

    if (pm.hasParam("isMale")) {
        isModified = true;
        const auto v = pm.getParam("isMale");
        bool male = (DataFormatUtil::toLowerCase(v) == std::string("true"));
        user.setIsMale(male);
    }

    if (pm.hasParam("user_password")) {
        isModified = true;
        auto [hash, salt] = CreateStrPasswordHash(pm.getParam("user_password"));
        user.setPasswordHash(hash);
        user.setPasswordSalt(salt);
        // 密码更改, 登录应该失效, 这里通过更新status的方式
        try{
            auto tokenRow = mapperUserFlashtoken.findOne(Criteria(UserFlashtoken::Cols::_user_id, CompareOperator::EQ, userId));
            tokenRow.setStatus(-1);
            tokenRow.setStatusForToken(-1);
            mapperUserFlashtoken.update(tokenRow);
        }catch(const UnexpectedRows &){
            result.setResult(-2, "内部错误");
            co_return result;
        }
    }

    if (!isModified) {
        result.setResult(-1, "没有修改项");
        co_return result;
    }

    auto ret = mapperUser.update(user);
    if (ret != 1) {
        result.setResult(-1, "更新失败");
        co_return result;
    }

    result.setResult(0, "更新成功");
    co_return result;
}

drogon::Task<UEAdminAPI::utils::HttpResult> AuthService::DeleteUser(
    const std::string &token,
    const std::string &target,
    const std::string &verifyCode) {
    auto _mfaService = MFAService::Instance();
    auto _gitlabService = UEAdminAPI::GitlabService::Instance();
    UEAdminAPI::utils::HttpResult result;
    std::vector<std::string> missing;
    if (token.empty()) missing.push_back("token");
    if (target.empty()) missing.push_back("target");
    if (verifyCode.empty()) missing.push_back("verifyCode");
    if (!missing.empty()) {
        std::string msg = "缺少必填项: " + std::accumulate(missing.begin(), missing.end(), std::string(), [](const std::string& a, const std::string& b){ return a.empty() ? b : a + ", " + b; });
        result.setResult(-1, msg);
        co_return result;
    }

    result = co_await VerifyToken(token);
    if(result.jsondata["tokenType"].asString() != "token"){
        result.setResult(-1, "不是token");
        co_return result;
    }
    if(result.code != 0){
        result.setResult(-1, "Token已失效");
        co_return result;
    }
    int userId = result.jsondata["userId"].asInt();

    auto dbClientPtr = drogon::app().getDbClient();
    Mapper<User> mapperUser(dbClientPtr);
    User user;
    try {
        user = mapperUser.findByPrimaryKey(userId);
    } catch (...) {
        result.setResult(-1, "用户不存在");
        co_return result;
    }

    auto chType2 = MFAChannelBase::DetermineChannelType(target);
    if (chType2 == eChannelType::Email) {
        if (!user.getEmail() || user.getValueOfEmail() != target) {
            result.setResult(-1, "目标未绑定到当前用户");
            co_return result;
        }
    } else if (chType2 == eChannelType::SMS) {
        auto [cc, pn] = CodePairBase::SMSCodePair::ParsePhoneNumber(target);
        if (!user.getTelephoneNumber() || user.getValueOfTelephoneNumber() != pn) {
            result.setResult(-1, "目标未绑定到当前用户");
            co_return result;
        }
    } else {
        result.setResult(-1, "无法判断渠道类型");
        co_return result;
    }

    auto [mfaOk, mfaErr] = co_await _mfaService->VerifyTheCode(target, verifyCode, eMFAType::DeleteUser);
    if (!mfaOk) {
        result.setResult(-1, mfaErr.empty() ? std::string("验证码错误") : mfaErr);
        co_return result;
    }

    Mapper<UserThirdPartyInfo> mapperThird(dbClientPtr);
    Mapper<UserGitlabInfo> mapperGit(dbClientPtr);
    Mapper<UserFlashtoken> mapperToken(dbClientPtr);

    // 同步删除 GitLab 用户
    try {
        auto gitInfo = mapperGit.findOne(Criteria(UserGitlabInfo::Cols::_user_id, CompareOperator::EQ, userId));
        auto gitlabId = gitInfo.getValueOfGitId();
        if (gitlabId > 0) {
            // 先尝试删除模拟令牌（若存在）
            if (gitInfo.getGitlabImpersonationTokenId()) {
                (void)_gitlabService->deleteImpersonationToken(static_cast<uint32_t>(gitlabId), static_cast<uint32_t>(gitInfo.getValueOfGitlabImpersonationTokenId()));
            }
            if (!_gitlabService->deleteUser(static_cast<uint32_t>(gitlabId))) {
                result.setResult(-1, "删除 GitLab 用户失败");
                co_return result;
            }
        }
        // 远端删除成功后，删除本地 GitLab 关联信息
        try {
            mapperGit.deleteOne(gitInfo);
        } catch (...) {}
    } catch (...) {
        // 未绑定 GitLab 或查询失败，忽略远端删除
    }

    try {
        mapperThird.deleteBy(Criteria(UserThirdPartyInfo::Cols::_user_id, CompareOperator::EQ, userId));
    } catch (...) {
    }
    try {
        mapperToken.deleteBy(Criteria(UserFlashtoken::Cols::_user_id, CompareOperator::EQ, userId));
    } catch (...) {
    }

    bool deleted = false;
    try {
        auto user = mapperUser.findByPrimaryKey(userId);
        mapperUser.deleteOne(user);
        deleted = true;
    } catch (...) {
        deleted = false;
    }

    if (!deleted) {
        result.setResult(-1, "删除失败");
        co_return result;
    }

    result.setResult(0, "删除成功");
    co_return result;
}

drogon::Task<std::tuple<bool, std::string>> AuthService::VerifyUserTargetMFA(
    const std::string &target,
    const std::string &verifyCode,
    int userId,
    eMFAType type) 
{
    auto dbClientPtr = drogon::app().getDbClient();
    Mapper<User> mapperUser(dbClientPtr);
    User user;
    bool isUserFound = true;
    try {
        user = mapperUser.findByPrimaryKey(userId);
    } catch (const UnexpectedRows &) {
        isUserFound = false;
    }
    
    if (!isUserFound) {
        co_return std::make_tuple(false, std::string("用户不存在"));
    }

    co_return co_await VerifyUserTargetMFA(target, verifyCode, user, type);
}

drogon::Task<std::tuple<bool, std::string>> AuthService::VerifyUserTargetMFA(const std::string &target, const std::string &verifyCode, const drogon_model::UEAdminAPI::User &user, eMFAType type) {
    auto _mfaService = MFAService::Instance();

    if (target.empty() || verifyCode.empty() || user.getValueOfId() <= 0) {
        co_return std::make_tuple(false, std::string("缺少必要参数"));
    }

    auto chType = MFAChannelBase::DetermineChannelType(target);
    if (chType == eChannelType::Email) {
        if (!user.getEmail() || user.getValueOfEmail() != target) {
            co_return std::make_tuple(false, std::string("目标未绑定到当前用户"));
        }
    } else if (chType == eChannelType::SMS) {
        auto [cc, pn] = CodePairBase::SMSCodePair::ParsePhoneNumber(target);
        if (!user.getTelephoneNumber() || user.getValueOfTelephoneNumber() != pn) {
            co_return std::make_tuple(false, std::string("目标未绑定到当前用户"));
        }
    } else {
        co_return std::make_tuple(false, std::string("无法判断渠道类型"));
    }

    auto [ok, msg] = co_await _mfaService->VerifyTheCode(target, verifyCode, type);
    if (!ok) {
        co_return std::make_tuple(false, msg.empty() ? std::string("验证码错误") : msg);
    }

    co_return std::make_tuple(true, std::string());
}

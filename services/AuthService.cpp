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
#include "services/ThirdPartyLoginService.h"

#include "models/UserFlashtoken.h"
#include "models/User.h"
#include "models/UserGitlabInfo.h"
#include "models/UserThirdPartyInfo.h"
#include "models/ThirdpartyPlatforms.h"

#include <utils/EnumUserPrivileges.h>
#include <utils/RandomGenerator.h>
#include <utils/DataFormatUtils.h>

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

std::tuple<bool, int, int, int> AuthService::CheckTokenAndParseUserId(const std::string &token) {
    // 解码并验证token
    auto decoded = jwt::decode(token);
    auto verifier = jwt::verify()
        .allow_algorithm(jwt::algorithm::hs512{_secret})
        .with_issuer(_jwtIssuer);

    std::string tokenType;
    int status;
    int32_t userId;
    int isFlashToken;
    try{
        tokenType = decoded.get_payload_claim("tokenType").as_string();
        if(tokenType != "token" && tokenType != "flashToken"){
            LOG_ERROR << std::format("对于Token: {}, tokenType 既不是 token, 也不是 flashToken", token);
            return std::make_tuple(false, -1, -1, -1);
        }
        isFlashToken = tokenType == "flashToken";
        status = std::stoi(decoded.get_payload_claim("status").as_string());
        userId = std::stoi(decoded.get_payload_claim("id").as_string());
    }
    catch (const std::exception &e) {
        LOG_ERROR << std::format("对于Token: {}, 有缺失的 Claim: {}", token, e.what());
        return std::make_tuple(false, -1, -1, -1);
    }
    try {
        verifier.verify(decoded); 

        // 获取用户ID
        auto userIdClaim = decoded.get_payload_claim("id");

        return std::make_tuple(true, userId, status, isFlashToken);
    }
    catch (const std::exception &e) {
        LOG_ERROR << std::format("对于Token: {}, 验证失败: {}", token, e.what());
        LOG_ERROR << "Token验证失败: " << e.what();
        return std::make_tuple(false, -1, status, isFlashToken);
    }
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

    if(isFlashToken)
        if(token.getValueOfStatus() != status)
            co_return false;
    else
        if(token.getValueOfStatusForToken() != status)
            co_return false;

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

    // 随机生成数字作为状态
    int status = RandomGenerator::getInt(1, INT_MAX);
    string flashToken = CreateFlashToken(targetUser.getValueOfId(), status);
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
            co_return result;
        }
    }

    auto token = CreateToken(targetUser.getValueOfId(), status);

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
        co_return result;
    }

    // 根据刚找到的用户的关系从数据库获取对应的user_flash_token的记录
    // 如果没找到则创建一条新的FlashToken
    UserFlashtoken flashtoken;
    try{
        flashtoken = targetUser.getUser_flashtoken(dbClientPtr);
    }
    catch (UnexpectedRows& e){
        // 没找到则创建
        flashtoken.setUserId(targetUser.getValueOfId());
        mapperFlashToken.insert(flashtoken);
    }

    // 创建新的FlashToken
    // 生成随机数作为status, status用于标记最新的token 
    // (虽说这样是抛弃了JWT无状态的优点, 但也只是存了一个int数, 开销不大, 免去了要处理同时有多个FlashToken可用的问题的麻烦)
    int status = RandomGenerator::getInt(1, INT_MAX);
    string flashToken = CreateFlashToken(targetUser.getValueOfId(), status);
    // 更新状态到数据库
    flashtoken.setStatus(status);
    auto ret = mapperFlashToken.update(flashtoken);
    if (ret != 1) {
        result.setResult(-1, "更新状态失败");
        co_return result;
    }

    // Token还未实际使用status状态变量, 仅FlashToken在使用, 所以这里的status仅占位
    string token = CreateToken(targetUser.getValueOfId(), status);

    result.jsondata["flashToken"] = flashToken;
    result.jsondata["token"] = token;
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

    UserFlashtoken flashtoken;
    bool isNeedNewCreate = false;
    try {
        flashtoken = targetUser.getUser_flashtoken(dbClientPtr);
    } catch (UnexpectedRows& e) {
        if(strcmp(e.what(), "0 rows found") != 0){
            throw e;
        }
        isNeedNewCreate = true;
    }

    if(isNeedNewCreate){
        flashtoken.setUserId(targetUser.getValueOfId());
        flashtoken.setStatus(0); // 初始状态
        try{
            mapperFlashToken.insert(flashtoken);
        }
        catch (std::runtime_error& e){
            LOG_ERROR << "LoginByUserId 新建flashtoken插入时出错!";
            throw e;
        }
    }

    int status = RandomGenerator::getInt(1, INT_MAX);
    std::string flashTokenStr = CreateFlashToken(targetUser.getValueOfId(), status);
    
    flashtoken.setStatus(status);
    auto ret = mapperFlashToken.update(flashtoken);
    if (ret != 1) {
        result.setResult(-1, "更新状态失败");
        co_return result;
    }

    std::string token = CreateToken(targetUser.getValueOfId(), status);

    result.jsondata["flashToken"] = flashTokenStr;
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

    std::string token = CreateToken(userId, status);
    result.jsondata["token"] = token;
    result.jsondata["flashToken"] = flashToken;
    result.jsondata["id"] = userId;
    result.setResult(0, "刷新成功");
    co_return result;
}

drogon::Task<UEAdminAPI::utils::HttpResult> AuthService::GetSelfInfo(const std::string &token) {
    HttpResult result;

    if(token.empty()){
        result.setResult(-1, "缺少必要参数");
        co_return result;
    }

    auto [isSuccess, userId, status, isFlashToken] = CheckTokenAndParseUserId(token);
    if(!isSuccess || userId == -1){
        result.setResult(-1, "身份验证失败");
        co_return result;
    }

    bool valid = co_await CheckTokenStatus(userId, status, isFlashToken == 1);
    if(!valid){
        result.setResult(-1, "Token已失效");
        co_return result;
    }

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

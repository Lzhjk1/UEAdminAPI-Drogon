#pragma once
#include "utils/SingletonWithInit.h"
#include "utils/EnumUserPrivileges.h"
#include "models/User.h"
#include "json/json.h"
#include <iostream>
#include <string>
#include "utils/HttpResult.h"
#include "utils/PostParamMap.h"
#include <drogon/drogon.h>
#include <unordered_map>
#include "utils/MFA/eMFA_Type.h"

class AuthService : public SingletonWithInit<AuthService> {
private:
    std::string _secret;
    std::string _jwtIssuer;
    uint64_t _tokenExpireSec = 3600;
    uint64_t _flashTokenExpireSec = 259200;

    // RS256 非对称签名
    std::string _privateKeyPem;   ///< RSA 私钥 (PEM)
    std::string _publicKeyPem;    ///< RSA 公钥 (PEM)

    /// 初始化 RSA 密钥对（文件 / 配置 PEM / 自动生成）
    void initRsaKeys(const Json::Value &config);

public:
    /// @brief 获取 RSA 公钥 PEM（供 OAuth2Controller JWKS 端点使用）
    const std::string& getPublicKeyPem() const { return _publicKeyPem; }

    AuthService(const Json::Value &config);

    std::vector<unsigned char> stringToVector(const std::string &str);
    std::string vectorToString(const std::vector<unsigned char> &vec);
    std::string vectorToString(const std::vector<char>& vec);

    // 前Hash, 后Salt, 返回字节数组
    std::tuple<std::vector<unsigned char>, std::vector<unsigned char>> CreatePasswordHash(const std::string &password);
    // 前Hash, 后Salt, 返回字符串
    std::tuple<std::string, std::string> CreateStrPasswordHash(const std::string &password);

    bool VerifyPasswordHash(const std::string &password,
                            const std::vector<unsigned char> &hash,
                            const std::vector<unsigned char> &salt);

    bool VerifyPasswordHash(const std::string &password,
                            const std::shared_ptr<std::vector<char>> &hashPtr,
                            const std::shared_ptr<std::vector<char>> &saltPtr);

    /// @brief 创建一个Token
    /// @param id 用户Id
    /// @param status 状态码, 用于在数据库中标记一个Token, 以实现旧Token失效机制
    /// @param durationSeconds Token有效期, 单位为秒, 默认使用配置中的tokenExpeirSec或3600
    /// @return 
    std::string CreateToken(int id, int status, uint64_t durationSeconds = 0); 
    
    /// @brief 创建一个FlashToken
    /// @param id 用户Id
    /// @param status 状态码, 用于在数据库中标记一个Token, 以实现旧Token失效机制
    /// @param durationSeconds Token有效期, 单位为秒, 默认使用配置中的tokenFlashTokenSec或259200
    /// @return 
    std::string CreateFlashToken(int id, int status, uint64_t durationSeconds = 0); 

    // 颁发一对 Token 与 FlashToken，并将 status 同步到数据库
    drogon::Task<std::tuple<std::string, std::string, int>> NewTokenPair(int userId);

    // 根据已有的 status 颁发 Token, 若未提供 status 则随机一个，并把 status 写入数据库
    drogon::Task<std::tuple<std::string, int>> NewToken(int userId, int status = -1);

    /// @brief 从Token解析用户ID, Token不合法或过期失效时返回-1
    /// @param token Token
    /// @return 元组[总体成功与否, 用户Id, 状态码, 是否为flashToken(int)], token失效过期时id为-1, 仍会正常获取tokenType, 如果tokentype也没有则会返回-1
    std::tuple<bool, int, int, int> CheckTokenAndParseUserId(const std::string &token);

    /// @brief 检查用户是否存在(通过邮箱或手机号)
    /// @param email 邮箱 (可选)
    /// @param phone 手机号 (可选)
    /// @return true: 存在, false: 不存在
    drogon::Task<bool> CheckUserExist(const std::string &email, const std::string &phone);

    /// @brief 检查用户是否存在(智能判断是邮箱还是手机号)
    /// @param target 邮箱或手机号
    /// @return true: 存在, false: 不存在
    drogon::Task<bool> CheckUserExist(const std::string &target);

    /// @brief 邮箱注册用户
    /// @param username 用户名
    /// @param password 密码
    /// @param email 邮箱
    /// @param privilege 权限
    /// @param isMale 性别
    /// @param nickname 昵称, 默认为空, 为空时使用用户名
    /// @return HttpResult
    drogon::Task<UEAdminAPI::utils::HttpResult> RegisterByEmail(
        const std::string &username, 
        const std::string &password, 
        const std::string &email, 
        const UserPrivileges &privilege = UserPrivileges::User, 
        const bool &isMale = true, 
        const std::string &nickname = "");

    /// @brief 手机号注册用户
    /// @param username 用户名
    /// @param password 密码
    /// @param phoneNumber 手机号
    /// @param privilege 权限
    /// @param isMale 性别
    /// @param nickname 昵称, 默认为空, 为空时使用用户名
    /// @return HttpResult
    drogon::Task<UEAdminAPI::utils::HttpResult> RegisterByPhone(
        const std::string &username, 
        const std::string &password, 
        const std::string &phoneNumber, 
        const UserPrivileges &privilege = UserPrivileges::User, 
        const bool &isMale = true, 
        const std::string &nickname = "");

    /// @brief 通过用户ID直接登录 (内部使用)
    /// @param userId 用户ID
    /// @return HttpResult 包含Token信息
    drogon::Task<UEAdminAPI::utils::HttpResult> LoginByUserId(int userId);

    /// @brief 附带第三方平台的邮箱注册
    /// 目前在用户注册后, 才插入第三方平台信息, 所以可能出现用户创建成功但第三方平台信息未绑定的情况
    /// 未做回滚处理, 若发生此情况, 会在返回信息写明, 并且jsondata["userCreatedButThirdPartyNotBind"]为true
    drogon::Task<UEAdminAPI::utils::HttpResult> RegisterWithThirdPartyByEmail(
       const std::string &username, 
       const std::string &password, 
       const std::string &email, 
       const std::string &third_platform_name,
       const std::string &third_code,
       const std::string &third_verifyCode,
       const UserPrivileges &privilege = UserPrivileges::User, 
       const bool &isMale = true,
       const std::string &nickname = "");

    /// @brief 附带第三方平台的电话注册
    drogon::Task<UEAdminAPI::utils::HttpResult> RegisterWithThirdPartyByPhone(
       const std::string &username, 
       const std::string &password, 
       const std::string &phoneNumber, 
       const std::string &third_platform_name,
       const std::string &third_code,
       const std::string &third_verifyCode,
       const UserPrivileges &privilege = UserPrivileges::User, 
       const bool &isMale = true,
       const std::string &nickname = ""
    );


    // 检查指定用户的token的Status, 返回true表示有效, false表示无效
    drogon::Task<bool> CheckTokenStatus(const int &userId, const int &status, const bool &isFlashToken = false);

    /// @brief 通过密码登录
    /// @param userName 用户名
    /// @param pwd 密码
    /// @return 成功返回示例为:
    /// {
    ///     "code": 200,
    ///     "data": {
    ///         "token": "token",
    ///         "flashToken": "flashToken"
    ///     }
    ///     "msg": "success"
    /// }
    drogon::Task<UEAdminAPI::utils::HttpResult> LoginByPwd(const std::string &userName, const std::string &pwd);

    /// @brief 通过其他方式, 如邮箱, 手机验证码登录
    /// @param target 目标, 如邮箱, 手机号
    /// @param targetDBColName 目标在数据库的列名, 建议通过orm对象获取, 如User::Cols::_email
    /// @param mfaCode 预先通过验证码SendVerifyCode控制器发送的验证码
    /// @return
    drogon::Task<UEAdminAPI::utils::HttpResult> LoginByOther(const std::string &target, const std::string &targetDBColName);

    /// @brief 通过邮箱验证码登录
    /// @param email 邮箱
    /// @return
    drogon::Task<UEAdminAPI::utils::HttpResult> LoginByEmail(const std::string &email);

    /// @brief 通过手机验证码登录
    /// @param phone 手机号
    /// @return
    drogon::Task<UEAdminAPI::utils::HttpResult> LoginByPhone(const std::string &phone);

    drogon::Task<UEAdminAPI::utils::HttpResult> LoginByFlashToken(const std::string &flashToken);

    drogon::Task<UEAdminAPI::utils::HttpResult> GetSelfInfo(int userId);

    // 验证Token并返回解析信息
    drogon::Task<UEAdminAPI::utils::HttpResult> VerifyToken(const std::string &token);

    // 注销登录 (使当前 Token 失效)
    drogon::Task<UEAdminAPI::utils::HttpResult> Logout(int userId);

    // 更新当前登录用户的信息（直接接收 PostParamMap）
    // 注意: 操作权限已经由 ActionTokenFilter 验证。
    drogon::Task<UEAdminAPI::utils::HttpResult> UpdateUserInfo(
        int userId,
        const UEAdminAPI::PostParamMap &pm);

    // 删除当前登录用户
    drogon::Task<UEAdminAPI::utils::HttpResult> DeleteUser(
        int userId,
        const std::string &target,
        const std::string &mfaCode);

    // 强制删除用户 (不验证验证码, 仅供内部或管理员使用)
    drogon::Task<UEAdminAPI::utils::HttpResult> DeleteUserForce(int userId);

    // 验证指定用户的绑定目标并校验MFA验证码
    // 重载: 传入userId, 从数据读取user记录对象后, 再传入主要函数
    drogon::Task<std::tuple<bool, std::string>> VerifyUserTargetMFA(
        const std::string &target,
        const std::string &mfaCode,
        int userId,
        eMFAType type);
    // 验证指定用户的绑定目标并校验MFA验证码
    drogon::Task<std::tuple<bool, std::string>> VerifyUserTargetMFA(
        const std::string &target,
        const std::string &mfaCode,
        const drogon_model::UEAdminAPI::User &user,
        eMFAType type);

    /**
     * @brief 核心注册事务处理：Gitlab创建 -> 数据库插入 -> 异常回滚
     * @param preparedUser 已经填充好基础信息（密码哈希、昵称、时间等）的User对象
     * @param rawPassword 原始密码（用于Gitlab账号创建）
     * @param gitlabEmail 用于Gitlab注册的邮箱（手机注册和第三方注册时为假邮箱）
     * @return HttpResult
     */
    drogon::Task<UEAdminAPI::utils::HttpResult> ExecuteRegistrationTransaction(
        drogon_model::UEAdminAPI::User &preparedUser, 
        const std::string &rawPassword, 
        const std::string &gitlabEmail
    );
    
    // 辅助函数：检查字段是否存在 (简化查重逻辑)
    drogon::Task<bool> CheckIfExist(drogon::orm::Mapper<drogon_model::UEAdminAPI::User>& mapper, const drogon::orm::Criteria& criteria);
};

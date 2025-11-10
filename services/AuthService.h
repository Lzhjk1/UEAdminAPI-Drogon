#pragma once
#include "utils/SingletonWithInit.h"
#include "models/User.h"
#include "json/json.h"
#include <iostream>
#include <string>

using namespace drogon_model::UEAdminAPI;

class AuthService : public SingletonWithInit<AuthService> {
private:
    std::string _secret;
    std::string _jwtIssuer;

public:
    AuthService(const Json::Value &config);

    std::vector<unsigned char> stringToVector(const std::string &str);
    std::string vectorToString(const std::vector<unsigned char> &vec);
    std::string vectorToString(const std::vector<char>& vec);

    std::tuple<std::vector<unsigned char>, std::vector<unsigned char>> CreatePasswordHash(const std::string &password);

    bool VerifyPasswordHash(const std::string &password,
                            const std::vector<unsigned char> &hash,
                            const std::vector<unsigned char> &salt);

    bool VerifyPasswordHash(const std::string &password,
                            const std::shared_ptr<std::vector<char>> &hashPtr,
                            const std::shared_ptr<std::vector<char>> &saltPtr);

    /// @brief 创建一个Token
    /// @param id 用户Id
    /// @param status 状态码, 用于在数据库中标记一个Token, 以实现旧Token失效机制
    /// @param durationSeconds Token有效期, 单位为秒, 默认1小时
    /// @return 
    std::string CreateToken(int id, int status, uint64_t durationSeconds = 3600); // 1小时 = 60 * 60
    
    /// @brief 创建一个FlashToken
    /// @param id 用户Id
    /// @param status 状态码, 用于在数据库中标记一个Token, 以实现旧Token失效机制
    /// @param durationSeconds Token有效期, 单位为秒, 默认3天
    /// @return 
    std::string CreateFlashToken(int id, int status, uint64_t durationSeconds = 259200); // 3天 = 3 * 24 * 60 * 60

    /// @brief 从Token解析用户ID, Token不合法或过期失效时返回-1
    /// @param token Token
    /// @return 用户Id, Token不合法或过期失效时返回 -1
    int CheckTokenAndParseUserId(const std::string &token);
};
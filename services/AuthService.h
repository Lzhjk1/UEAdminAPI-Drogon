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

    // 相比原版, 这两个函数生成的Token都有tokenType字段, 而且采用HS512算法
    std::string CreateToken(const User &user, uint64_t durationSeconds);
    std::string CreateFlashToken(const User &user, uint64_t durationSeconds = 1296000); // 15天 = 15 * 24 * 60 * 60
};
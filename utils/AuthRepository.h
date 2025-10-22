#pragma once
#include <vector>
#include <string>

#include "models/User.h"
using namespace drogon_model::UEAdminAPI;

namespace UEAdminAPI {
    namespace AuthRepository {
        static std::string SECRET;
        static std::string JWT_ISSUER;

        void CreatePasswordHash(const std::string& password, std::vector<unsigned char>& hash, std::vector<unsigned char>& salt);
        bool VerifyPasswordHash(const std::string& password, const std::vector<unsigned char>& hash, const std::vector<unsigned char>& salt);
        
        // 相比原版, 这两个函数生成的Token都有tokenType字段, 而且采用HS512算法
        std::string CreateToken(const User& user, uint64_t durationSeconds);
        std::string CreateFlashToken(const User& user, uint64_t durationSeconds);

        class TokenManager{};
    }
}

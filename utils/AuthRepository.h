#pragma once
#include <vector>
#include <string>

#include "User.h"
using namespace drogon_model::UEAdminAPI;

namespace UEAdminAPI {
    namespace AuthRepository {
        static std::string SECRET;
        static std::string JWT_ISSUER;

        void CreatePasswordHash(const std::string& password, std::vector<unsigned char>& hash, std::vector<unsigned char>& salt);
        bool VerifyPasswordHash(const std::string& password, const std::vector<unsigned char>& hash, const std::vector<unsigned char>& salt);
        
        // 相比邱建立的, 我的两个Token都有tokenType这个claim
        std::string CreateToken(const User& user, uint64_t durationSeconds);
        // 相比邱建立的, 我的两个Token都有tokenType这个claim
        std::string CreateFlashToken(const User& user, uint64_t durationSeconds);

        class TokenManager{};
    }
}

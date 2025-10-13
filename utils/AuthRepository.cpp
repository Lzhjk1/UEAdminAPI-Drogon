#include "AuthRepository.h"
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/open-source-parsers-jsoncpp/traits.h>
#include <jwt-cpp/traits/open-source-parsers-jsoncpp/defaults.h>

namespace UEAdminAPI {
    namespace AuthRepository {
        void CreatePasswordHash(const std::string &password, std::vector<unsigned char> &hash, std::vector<unsigned char> &salt) {
            // 生成随机 salt
            salt.resize(64);
            RAND_bytes(salt.data(), salt.size());

            // 计算 HMACSHA512
            unsigned int len = SHA512_DIGEST_LENGTH;
            hash.resize(len);
            HMAC(EVP_sha512(), salt.data(), salt.size(),
                 reinterpret_cast<const unsigned char *>(password.data()), password.size(),
                 hash.data(), &len);
        }

        bool VerifyPasswordHash(const std::string &password, const std::vector<unsigned char> &hash, const std::vector<unsigned char> &salt) {
            unsigned int len = SHA512_DIGEST_LENGTH;
            std::vector<unsigned char> computedHash(len);
            HMAC(EVP_sha512(), salt.data(), salt.size(),
                 reinterpret_cast<const unsigned char *>(password.data()), password.size(),
                 computedHash.data(), &len);
            return computedHash == hash;
        }


        
        std::string CreateToken(const User& user, uint64_t durationSeconds) {
            auto token = jwt::create()
                .set_issuer(JWT_ISSUER)
                .set_type("JWS")
                .set_payload_claim("id", jwt::claim(std::to_string(user.getValueOfId())))
                .set_payload_claim(std::string("tokenType"), jwt::claim(std::string("token")))
                // status是附加验证信息, 但目前没有使用, 只是一个随机数字
                .set_payload_claim(std::string("status"), jwt::claim(std::to_string(rand()) + std::to_string(rand())))
                .set_expires_in(std::chrono::seconds{ durationSeconds })
                .sign(jwt::algorithm::hs512{ SECRET });
            return token;
        }
        std::string CreateFlashToken(const User& user, uint64_t durationSeconds) {
            auto token = jwt::create()
                .set_issuer(JWT_ISSUER)
                .set_type("JWS")
                .set_payload_claim("id", jwt::claim(std::to_string(user.getValueOfId())))
                .set_payload_claim(std::string("tokenType"), jwt::claim(std::string("flashToken")))
                // status是附加验证信息, 但目前没有使用, 只是一个随机数字
                .set_payload_claim(std::string("status"), jwt::claim(std::to_string(rand()) + std::to_string(rand())))
                .set_expires_in(std::chrono::seconds{ durationSeconds })
                .sign(jwt::algorithm::hs512{ SECRET });
            return token;
        }
    }
}
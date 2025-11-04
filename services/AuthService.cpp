#include "AuthService.h"
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/open-source-parsers-jsoncpp/defaults.h>
#include <jwt-cpp/traits/open-source-parsers-jsoncpp/traits.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

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

std::string AuthService::CreateToken(const User &user, uint64_t durationSeconds) {
    auto token = jwt::create()
                     .set_issuer(_jwtIssuer)
                     .set_type("JWS")
                     .set_payload_claim("id", jwt::claim(std::to_string(user.getValueOfId())))
                     .set_payload_claim(std::string("tokenType"), jwt::claim(std::string("token")))
                     // status是附加验证信息, 但目前没有使用, 只是一个随机数字
                     .set_payload_claim(std::string("status"), jwt::claim(std::to_string(rand()) + std::to_string(rand())))
                     .set_expires_in(std::chrono::seconds{durationSeconds})
                     .sign(jwt::algorithm::hs512{ _secret });
    return token;
}
std::string AuthService::CreateFlashToken(const User &user, uint64_t durationSeconds) {
    auto token = jwt::create()
                     .set_issuer(_jwtIssuer)
                     .set_type("JWS")
                     .set_payload_claim("id", jwt::claim(std::to_string(user.getValueOfId())))
                     .set_payload_claim(std::string("tokenType"), jwt::claim(std::string("flashToken")))
                     // status是附加验证信息, 但目前没有使用, 只是一个随机数字
                     .set_payload_claim(std::string("status"), jwt::claim(std::to_string(rand()) + std::to_string(rand())))
                     .set_expires_in(std::chrono::seconds{durationSeconds})
                     .sign(jwt::algorithm::hs512{ _secret });
    return token;
}
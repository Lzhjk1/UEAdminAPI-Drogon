/// @file OAuth2Controller.cc
/// @brief OAuth2 标准端点实现 — JWKS / Introspection / Revocation

#include "OAuth2Controller.h"

#include <drogon/drogon.h>
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/open-source-parsers-jsoncpp/defaults.h>
#include <jwt-cpp/traits/open-source-parsers-jsoncpp/traits.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#pragma GCC diagnostic pop

#include "services/AuthService.h"
#include "utils/HttpResult.h"
#include "utils/DataFormatUtils.h"
#include "utils/ApiErrorCodes.h"
#include "models/UserFlashtoken.h"
#include "models/User.h"

using namespace UEAdminAPI;
using namespace UEAdminAPI::utils;

namespace {

/// @brief 从 AuthService 获取 RSA 公钥并编码为 JWK JSON 字符串
std::string getPublicKeyAsJwk() {
    auto authService = AuthService::Instance();
    std::string pubKeyPem = authService->getPublicKeyPem();
    if (pubKeyPem.empty()) return {};

    BIO *bio = BIO_new_mem_buf(pubKeyPem.data(), (int)pubKeyPem.size());
    if (!bio) return {};
    EVP_PKEY *pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) return {};

    std::string nB64, eB64;
    RSA *rsa = EVP_PKEY_get1_RSA(pkey);
    if (rsa) {
        const BIGNUM *bnN = nullptr, *bnE = nullptr, *bnD = nullptr;
        RSA_get0_key(rsa, &bnN, &bnE, &bnD);
        if (bnN && bnE) {
            int nLen = BN_num_bytes(bnN);
            int eLen = BN_num_bytes(bnE);
            std::vector<unsigned char> nBuf(nLen), eBuf(eLen);
            BN_bn2bin(bnN, nBuf.data());
            BN_bn2bin(bnE, eBuf.data());

            nB64 = drogon::utils::base64Encode(
                reinterpret_cast<const unsigned char*>(nBuf.data()), nBuf.size());
            eB64 = drogon::utils::base64Encode(
                reinterpret_cast<const unsigned char*>(eBuf.data()), eBuf.size());
            for (auto &c : nB64) { if (c == '+') c = '-'; else if (c == '/') c = '_'; }
            for (auto &c : eB64) { if (c == '+') c = '-'; else if (c == '/') c = '_'; }
            if (!nB64.empty() && nB64.back() == '=') nB64.pop_back();
            if (!eB64.empty() && eB64.back() == '=') eB64.pop_back();
        }
        RSA_free(rsa);
    }
    EVP_PKEY_free(pkey);
    if (nB64.empty() || eB64.empty()) return {};

    Json::Value jwk;
    jwk["kty"] = "RSA";
    jwk["alg"] = "RS256";
    jwk["use"] = "sig";
    jwk["kid"] = "ueadmin-rsa-1";
    jwk["n"]   = nB64;
    jwk["e"]   = eB64;

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, jwk);
}

} // namespace

/// @brief 获取 RSA 公钥 JWKS 端点
/// 返回 /.well-known/jwks.json，用于 OAuth2 客户端获取公钥验证 Token 签名
Task<HttpResponsePtr> OAuth2Controller::jwks(HttpRequestPtr req) {
    (void)req;
    auto resp = HttpResponse::newHttpResponse();
    std::string jwkStr = getPublicKeyAsJwk();
    if (jwkStr.empty()) {
        resp->setBody(R"({"keys":[]})");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }
    resp->setBody(R"({"keys":[)" + jwkStr + R"(]})");
    resp->setContentTypeCode(CT_APPLICATION_JSON);
    resp->addHeader("Cache-Control", "public, max-age=3600");
    co_return resp;
}

/// @brief Token 验证端点（OAuth2 Introspection）
/// 接受 token 参数（POST body 或 form），返回 Token 的活跃状态、用户信息及过期时间
Task<HttpResponsePtr> OAuth2Controller::introspect(HttpRequestPtr req) {
    auto resp = HttpResponse::newHttpResponse();
    resp->setContentTypeCode(CT_APPLICATION_JSON);

    std::string token;
    auto params = req->getParameters();
    auto it = params.find("token");
    if (it != params.end()) token = it->second;

    if (token.empty()) {
        auto body = req->getBody();
        if (!body.empty()) {
            std::string bodyStr(body.data(), body.size());
            auto pos = bodyStr.find("token=");
            if (pos != std::string::npos) {
                pos += 6;
                auto end = bodyStr.find('&', pos);
                if (end == std::string::npos) end = bodyStr.size();
                token = drogon::utils::urlDecode(bodyStr.substr(pos, end - pos));
            }
        }
    }

    if (token.empty()) {
        resp->setBody(R"({"active":false,"error":"token_required"})");
        co_return resp;
    }

    auto authService = AuthService::Instance();
    auto [isSuccess, userId, status, isFlashToken] = authService->CheckTokenAndParseUserId(token);

    Json::Value jbody;
    if (!isSuccess || userId <= 0) {
        jbody["active"] = false;
    } else {
        jbody["active"] = true;
        jbody["sub"]    = std::to_string(userId);
        jbody["user_id"] = userId;
        jbody["token_type"] = "Bearer";
        jbody["username"] = "";

        try {
            auto decoded = jwt::decode<jwt::traits::open_source_parsers_jsoncpp>(token);
            if (decoded.has_payload_claim("exp")) {
                auto expClaim = decoded.get_payload_claim("exp");
                auto expTime = std::chrono::system_clock::to_time_t(expClaim.as_date());
                jbody["exp"] = (Json::Value::Int64)expTime;
            }
        } catch (...) {}

        try {
            auto dbClientPtr = drogon::app().getDbClient();
            drogon::orm::Mapper<drogon_model::UEAdminAPI::User> mapper(dbClientPtr);
            auto user = mapper.findByPrimaryKey(userId);
            jbody["username"] = user.getValueOfName();
            if (user.getNickName()) jbody["nickname"] = user.getValueOfNickName();
            if (user.getEmail()) jbody["email"] = user.getValueOfEmail();
            if (user.getPrivilege()) {
                int priv = user.getValueOfPrivilege();
                Json::Value roles(Json::arrayValue);
                if (priv >= 2) roles.append("admin");
                else if (priv >= 1) roles.append("user");
                else roles.append("default");
                jbody["roles"] = roles;
            }
        } catch (...) {}
    }

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    resp->setBody(Json::writeString(builder, jbody));
    co_return resp;
}

/// @brief Token 吊销端点（OAuth2 Revocation）
/// 接受 token 参数，在数据库中使该用户的 FlashToken 状态失效，实现登出效果
Task<HttpResponsePtr> OAuth2Controller::revoke(HttpRequestPtr req) {
    auto resp = HttpResponse::newHttpResponse();
    resp->setContentTypeCode(CT_APPLICATION_JSON);

    std::string token;
    auto params = req->getParameters();
    auto it = params.find("token");
    if (it != params.end()) token = it->second;

    if (token.empty()) {
        auto body = req->getBody();
        if (!body.empty()) {
            std::string bodyStr(body.data(), body.size());
            auto pos = bodyStr.find("token=");
            if (pos != std::string::npos) {
                pos += 6;
                auto end = bodyStr.find('&', pos);
                if (end == std::string::npos) end = bodyStr.size();
                token = drogon::utils::urlDecode(bodyStr.substr(pos, end - pos));
            }
        }
    }

    if (token.empty()) {
        resp->setBody(R"({"result":"ok"})");
        co_return resp;
    }

    auto authService = AuthService::Instance();
    auto [isSuccess, userId, status, isFlashToken] = authService->CheckTokenAndParseUserId(token);

    if (isSuccess && userId > 0) {
        try {
            auto dbClientPtr = drogon::app().getDbClient();
            drogon::orm::Mapper<drogon_model::UEAdminAPI::UserFlashtoken> mapper(dbClientPtr);
            try {
                auto row = mapper.findByPrimaryKey(userId);
                row.setStatus(-1);
                row.setStatusForToken(-1);
                mapper.update(row);
            } catch (const drogon::orm::UnexpectedRows &) {}
        } catch (const std::exception &e) {
            LOG_ERROR << "Revoke error: " << e.what();
        }
    }

    resp->setBody(R"({"result":"ok"})");
    co_return resp;
}

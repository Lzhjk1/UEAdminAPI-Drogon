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
#include "services/DeviceLoginSessionService.h"
#include "utils/HttpResult.h"
#include "utils/DataFormatUtils.h"
#include "utils/ApiErrorCodes.h"
#include "utils/PostParamMap.h"
#include "models/UserFlashtoken.h"
#include "models/User.h"
#include "utils/EnumUserPrivileges.h"

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
    std::string jwkStr = getPublicKeyAsJwk();
    if (jwkStr.empty()) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody(R"({"keys":[]})");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }
    auto resp = HttpResponse::newHttpResponse();
    resp->setBody(R"({"keys":[)" + jwkStr + R"(]})");
    resp->setContentTypeCode(CT_APPLICATION_JSON);
    resp->addHeader("Cache-Control", "public, max-age=3600");
    co_return resp;
}

/// @brief Token 验证端点（OAuth2 Introspection）
/// 接受 token 参数（POST body 或 form），返回 Token 的活跃状态、用户信息及过期时间
Task<HttpResponsePtr> OAuth2Controller::introspect(HttpRequestPtr req) {
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
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody(R"({"active":false,"error":"token_required"})");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
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

        bool userExists = false;
        try {
            auto dbClientPtr = drogon::app().getDbClient();
            drogon::orm::Mapper<drogon_model::UEAdminAPI::User> mapper(dbClientPtr);
            auto user = mapper.findByPrimaryKey(userId);
            userExists = true;
            jbody["username"] = user.getValueOfName();
            if (user.getNickName()) jbody["nickname"] = user.getValueOfNickName();
            if (user.getEmail()) jbody["email"] = user.getValueOfEmail();
            if (user.getPrivilege()) {
                int priv = user.getValueOfPrivilege();
                Json::Value roles(Json::arrayValue);
                // privilege 是位标志(UserPrivileges): Default=1, User=2, Admin=4,
                // 必须按位判断; 旧代码用 priv>=2 做大小比较, 会把普通用户(2)误判成 admin
                if (priv & static_cast<int>(UserPrivileges::Admin)) roles.append("admin");
                if (priv & static_cast<int>(UserPrivileges::User)) roles.append("user");
                if (priv & static_cast<int>(UserPrivileges::Default)) roles.append("default");
                if (roles.empty()) roles.append("default");
                jbody["roles"] = roles;
            }
        } catch (const drogon::orm::UnexpectedRows &) {
            // 用户不存在: Token 即使签名有效也不应视为活跃
        } catch (const std::exception &e) {
            LOG_ERROR << "Introspect error: " << e.what();
        }

        // 签名有效但数据库中不存在对应用户时, 视为不活跃
        if (!userExists) {
            jbody.clear();
            jbody["active"] = false;
        }
    }

    co_return HttpResponse::newHttpJsonResponse(jbody);
}

/// @brief Token 吊销端点（OAuth2 Revocation）
/// 接受 token 参数，在数据库中使该用户的 FlashToken 状态失效，实现登出效果
Task<HttpResponsePtr> OAuth2Controller::revoke(HttpRequestPtr req) {
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
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody(R"({"result":"ok"})");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
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

    auto resp = HttpResponse::newHttpResponse();
    resp->setBody(R"({"result":"ok"})");
    resp->setContentTypeCode(CT_APPLICATION_JSON);
    co_return resp;
}

/// @brief 校验 redirect_uri 是否允许：仅放行 http://127.0.0.1 / http://localhost / http://[::1]
/// 使用严格 host 解析，防止 http://127.0.0.1.evil.com 之类的前缀绕过。
static bool isAllowedLoopbackRedirectUri(const std::string &uri) {
    if (uri.empty()) {
        return true; // 空 = 兼容旧 ueloginreturn 流程
    }
    if (uri.rfind("http://", 0) != 0) {
        return false; // 仅允许 http，禁止 https 之外的自定义 scheme
    }

    // 取出 host 部分（含端口），要求 host 恰好是 loopback 地址。
    size_t schemeLen = 7; // strlen("http://")
    size_t pathStart = uri.find_first_of("/?#", schemeLen);
    std::string hostPort = uri.substr(schemeLen, pathStart == std::string::npos ? std::string::npos : pathStart - schemeLen);
    if (hostPort.empty()) {
        return false;
    }

    // 支持 [::1]（含端口形式 [::1]:port）；其余按 host:port 解析。
    std::string host;
    if (hostPort[0] == '[') {
        auto closeBracket = hostPort.find(']');
        if (closeBracket == std::string::npos) {
            return false;
        }
        host = hostPort.substr(1, closeBracket - 1);
    } else {
        host = hostPort;
        auto colonPos = host.rfind(':');
        if (colonPos != std::string::npos) {
            host = host.substr(0, colonPos);
        }
    }

    return host == "127.0.0.1" || host == "localhost" || host == "::1";
}

namespace {

std::string ReadJsonOrForm(const HttpRequestPtr &req, const char *key) {
    auto json = req->getJsonObject();
    if (json && json->isMember(key)) {
        const auto &node = (*json)[key];
        if (node.isString()) {
            return node.asString();
        }
        if (node.isBool()) {
            return node.asBool() ? "true" : "false";
        }
    }
    return req->getParameter(key);
}

std::string ServerBaseUrl() {
    std::string host = drogon::app().getCustomConfig().get("ServerHost", "").asString();
    while (!host.empty() && host.back() == '/') {
        host.pop_back();
    }
    return host;
}

void AddNoStore(const HttpResponsePtr &resp) {
    resp->addHeader("Cache-Control", "no-store");
    resp->addHeader("Pragma", "no-cache");
}

HttpResponsePtr TokenError(int status, const std::string &error,
                           const std::string &description, int interval = 0) {
    Json::Value body;
    body["error"] = error;
    if (!description.empty()) {
        body["error_description"] = description;
    }
    if (interval > 0) {
        body["interval"] = interval;
    }
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(static_cast<HttpStatusCode>(status));
    AddNoStore(resp);
    return resp;
}

HttpResponsePtr RenderLoginPage(const std::string &userCode, const std::string &redirectUri) {
    HttpViewData viewData;
    viewData.insertAsString("userCode", HttpViewData::htmlTranslate(userCode));
    viewData.insertAsString("redirectUri", HttpViewData::htmlTranslate(redirectUri));
    auto resp = HttpResponse::newHttpViewResponse("oauth2_login.csp", viewData);
    resp->setStatusCode(k200OK);
    return resp;
}

HttpResponsePtr RenderDeviceVerify(const std::string &userCode, const std::string &error) {
    HttpViewData viewData;
    viewData.insertAsString("userCode", HttpViewData::htmlTranslate(userCode));
    viewData.insertAsString("error", HttpViewData::htmlTranslate(error));
    auto resp = HttpResponse::newHttpViewResponse("device_verify.csp", viewData);
    resp->setStatusCode(error.empty() ? k200OK : k400BadRequest);
    return resp;
}

int TokenExpireSeconds() {
    const auto cfg = drogon::app().getCustomConfig();
    if (cfg.isMember("UserManage") && cfg["UserManage"].isMember("tokenExpeirSec") &&
        cfg["UserManage"]["tokenExpeirSec"].isInt()) {
        return cfg["UserManage"]["tokenExpeirSec"].asInt();
    }
    return 720;
}

} // namespace

/// @brief POST /api/oauth2/device_authorization — 发起 RFC 8628 设备登录
Task<HttpResponsePtr> OAuth2Controller::loginStart(HttpRequestPtr req) {
    HttpResult result;
    std::string redirectUri;

    auto reqJson = req->getJsonObject();
    if (reqJson && reqJson->isMember("redirect_uri")) {
        redirectUri = (*reqJson)["redirect_uri"].asString();
    }

    if (!isAllowedLoopbackRedirectUri(redirectUri)) {
        result.setResult(ApiErrorCode::ApiError_DeviceLoginRedirectUriNotAllowed,
                         "redirect_uri 仅允许 http://127.0.0.1 / http://localhost / http://[::1]");
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k400BadRequest);
        co_return resp;
    }

    auto sessionService = UEAdminAPI::Services::DeviceLoginSessionService::Instance();
    if (!sessionService) {
        result.setResult(ApiErrorCode::ApiError_InternalError, "DeviceLoginSessionService 未初始化");
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k500InternalServerError);
        co_return resp;
    }

    auto auth = sessionService->CreateSession(redirectUri);
    if (auth.deviceCode.empty() || auth.userCode.empty()) {
        result.setResult(ApiErrorCode::ApiError_InternalError, "创建设备登录会话失败");
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k500InternalServerError);
        co_return resp;
    }

    const std::string base = ServerBaseUrl();
    const std::string verificationUri = base + "/device";
    const std::string verificationUriComplete =
        verificationUri + "?user_code=" + drogon::utils::urlEncodeComponent(auth.userCode);

    result.setResult(ApiErrorCode::ApiError_Success);
    result.jsondata["device_code"] = auth.deviceCode;
    result.jsondata["user_code"] = auth.userCode;
    result.jsondata["verification_uri"] = verificationUri;
    result.jsondata["verification_uri_complete"] = verificationUriComplete;
    result.jsondata["expires_in"] = auth.expiresIn;
    result.jsondata["interval"] = auth.interval;
    // 旧客户端只认 login_url/expires；这里给不含 device_code 的完整验证 URL。
    result.jsondata["login_url"] = verificationUriComplete;
    result.jsondata["expires"] = auth.expiresIn;

    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    AddNoStore(resp);
    co_return resp;
}

Task<HttpResponsePtr> OAuth2Controller::deviceToken(HttpRequestPtr req) {
    const std::string grantType = ReadJsonOrForm(req, "grant_type");
    if (!grantType.empty() &&
        grantType != "urn:ietf:params:oauth:grant-type:device_code" &&
        grantType != "device_code") {
        co_return TokenError(k400BadRequest, "unsupported_grant_type",
                             "仅支持 urn:ietf:params:oauth:grant-type:device_code");
    }

    const std::string deviceCode = ReadJsonOrForm(req, "device_code");
    if (deviceCode.empty()) {
        co_return TokenError(k400BadRequest, "invalid_request", "缺少 device_code");
    }

    auto sessionService = UEAdminAPI::Services::DeviceLoginSessionService::Instance();
    if (!sessionService) {
        co_return TokenError(k500InternalServerError, "server_error", "DeviceLoginSessionService 未初始化");
    }

    UEAdminAPI::Services::DeviceLoginSessionInfo session;
    const auto status = sessionService->PollAndConsume(deviceCode, session);
    if (status == UEAdminAPI::Services::DeviceLoginSessionService::PollStatus::NotFound) {
        co_return TokenError(k400BadRequest, "expired_token", "device_code 不存在、已过期或已使用");
    }
    if (status == UEAdminAPI::Services::DeviceLoginSessionService::PollStatus::SlowDown) {
        co_return TokenError(k400BadRequest, "slow_down", "轮询过快，请按 interval 轮询", session.intervalSec);
    }
    if (status == UEAdminAPI::Services::DeviceLoginSessionService::PollStatus::Pending) {
        co_return TokenError(k400BadRequest, "authorization_pending", "用户尚未完成授权");
    }

    auto authService = AuthService::Instance();
    auto [token, flashToken, tokenStatus] = co_await authService->NewTokenPair(session.userId);
    if (tokenStatus == -1) {
        co_return TokenError(k500InternalServerError, "server_error", "生成登录令牌失败");
    }

    std::string username;
    try {
        auto dbClientPtr = drogon::app().getDbClient();
        drogon::orm::Mapper<drogon_model::UEAdminAPI::User> mapper(dbClientPtr);
        auto user = mapper.findByPrimaryKey(session.userId);
        username = user.getValueOfName();
    } catch (const drogon::orm::UnexpectedRows &) {
        co_return TokenError(k400BadRequest, "access_denied", "用户不存在");
    } catch (const std::exception &e) {
        LOG_ERROR << "deviceToken 查询用户失败: " << e.what();
        co_return TokenError(k500InternalServerError, "server_error", "查询用户失败");
    }

    Json::Value body;
    body["access_token"] = token;
    body["token_type"] = "Bearer";
    body["expires_in"] = TokenExpireSeconds();
    body["refresh_token"] = flashToken;
    body["scope"] = "";
    // 兼容现有客户端字段
    body["token"] = token;
    body["flashToken"] = flashToken;
    body["id"] = session.userId;
    body["username"] = username;

    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(k200OK);
    AddNoStore(resp);
    co_return resp;
}

/// @brief GET /device — 输入 user_code 的验证页
Task<HttpResponsePtr> OAuth2Controller::deviceVerificationPage(HttpRequestPtr req) {
    const std::string userCode = req->getParameter("user_code");
    auto sessionService = UEAdminAPI::Services::DeviceLoginSessionService::Instance();
    if (!sessionService) {
        co_return RenderDeviceVerify("", "服务未初始化");
    }

    if (userCode.empty()) {
        co_return RenderDeviceVerify("", "");
    }

    auto session = sessionService->FindByUserCode(userCode);
    if (!session) {
        co_return RenderDeviceVerify(userCode, "验证码不存在或已过期");
    }
    if (session->userId > 0) {
        co_return RenderDeviceVerify("", "该设备登录已完成，请回到客户端");
    }
    co_return RenderLoginPage(userCode, session->redirectUri);
}

/// @brief POST /device — 校验 user_code 后进入登录页
Task<HttpResponsePtr> OAuth2Controller::deviceVerificationSubmit(HttpRequestPtr req) {
    const std::string userCode = ReadJsonOrForm(req, "user_code");
    auto sessionService = UEAdminAPI::Services::DeviceLoginSessionService::Instance();
    if (!sessionService) {
        co_return RenderDeviceVerify(userCode, "服务未初始化");
    }
    if (userCode.empty()) {
        co_return RenderDeviceVerify("", "请输入设备上显示的验证码");
    }

    auto session = sessionService->FindByUserCode(userCode);
    if (!session) {
        co_return RenderDeviceVerify(userCode, "验证码不存在或已过期");
    }
    if (session->userId > 0) {
        co_return RenderDeviceVerify("", "该设备登录已完成，请回到客户端");
    }
    co_return RenderLoginPage(userCode, session->redirectUri);
}

/// @brief GET /login — 设备登录页
Task<HttpResponsePtr> OAuth2Controller::loginPage(HttpRequestPtr req) {
    std::string userCode = req->getParameter("user_code");
    if (userCode.empty()) {
        userCode = req->getParameter("state"); // 旧链接兼容：按 user_code 解析，旧 secret state 不会命中
    }

    auto sessionService = UEAdminAPI::Services::DeviceLoginSessionService::Instance();
    if (!sessionService) {
        co_return RenderDeviceVerify("", "服务未初始化");
    }
    if (userCode.empty()) {
        co_return RenderDeviceVerify("", "");
    }

    auto session = sessionService->FindByUserCode(userCode);
    if (!session) {
        co_return RenderDeviceVerify(userCode, "验证码不存在或已过期");
    }
    if (session->userId > 0) {
        co_return RenderDeviceVerify("", "该设备登录已完成，请回到客户端");
    }
    co_return RenderLoginPage(userCode, session->redirectUri);
}

/// @brief POST /login — 设备登录页账号密码登录
Task<HttpResponsePtr> OAuth2Controller::loginByPwd(HttpRequestPtr req) {
    auto contentType = req->getHeader("content-type");
    const bool isFormSubmit = (contentType.find("application/x-www-form-urlencoded") != std::string::npos) ||
                              (contentType.find("multipart/form-data") != std::string::npos);

    auto reqJson = req->getJsonObject();
    if (!reqJson && isFormSubmit) {
        auto params = req->getParameters();
        Json::Value formJson(Json::objectValue);
        for (const char *key : {"user_code", "state", "userName", "passWord", "redirect_uri"}) {
            if (params.find(key) != params.end()) {
                formJson[key] = params[key];
            }
        }
        reqJson = std::make_shared<Json::Value>(formJson);
    }
    if (!reqJson) {
        co_return TokenError(k400BadRequest, "invalid_request", "请求体必须是 JSON 或表单");
    }

    std::string userCode = ReadJsonOrForm(req, "user_code");
    if (userCode.empty()) {
        userCode = ReadJsonOrForm(req, "state");
    }
    const std::string userName = ReadJsonOrForm(req, "userName");
    const std::string passWord = ReadJsonOrForm(req, "passWord");
    const std::string redirectUri = ReadJsonOrForm(req, "redirect_uri");

    auto failWithFormRedirect = [&](const std::string &errorMsg) -> HttpResponsePtr {
        auto resp = HttpResponse::newRedirectionResponse(
            "/login?user_code=" + drogon::utils::urlEncodeComponent(userCode) +
            "&error=" + drogon::utils::urlEncodeComponent(errorMsg));
        resp->setStatusCode(k303SeeOther);
        return resp;
    };

    if (userCode.empty() || userName.empty() || passWord.empty()) {
        if (isFormSubmit) {
            co_return failWithFormRedirect("缺少 user_code、userName 或 passWord");
        }
        co_return TokenError(k400BadRequest, "invalid_request", "缺少 user_code、userName 或 passWord");
    }

    auto sessionService = UEAdminAPI::Services::DeviceLoginSessionService::Instance();
    if (!sessionService) {
        if (isFormSubmit) {
            co_return failWithFormRedirect("服务异常，请稍后重试");
        }
        co_return TokenError(k500InternalServerError, "server_error", "DeviceLoginSessionService 未初始化");
    }

    auto session = sessionService->FindByUserCode(userCode);
    if (!session) {
        if (isFormSubmit) {
            co_return failWithFormRedirect("验证码不存在或已过期，请回到客户端重新发起登录");
        }
        co_return TokenError(k400BadRequest, "expired_token", "user_code 不存在或已过期");
    }
    if (session->userId > 0) {
        if (isFormSubmit) {
            co_return failWithFormRedirect("该设备登录已完成，请回到客户端");
        }
        co_return TokenError(k400BadRequest, "access_denied", "该设备登录已完成");
    }
    if (!redirectUri.empty() && redirectUri != session->redirectUri) {
        if (isFormSubmit) {
            co_return failWithFormRedirect("redirect_uri 与登录会话不一致");
        }
        co_return TokenError(k400BadRequest, "invalid_request", "redirect_uri 与登录会话不一致");
    }

    auto authService = AuthService::Instance();
    auto loginResult = co_await authService->LoginByPwd(userName, passWord);
    if (loginResult.code != 0) {
        if (isFormSubmit) {
            co_return failWithFormRedirect(loginResult.msg);
        }
        auto resp = HttpResponse::newHttpJsonResponse(loginResult.toJson());
        resp->setStatusCode(k401Unauthorized);
        co_return resp;
    }

    const int userId = loginResult.jsondata["id"].asInt();
    if (!sessionService->MarkLoggedInByUserCode(userCode, userId)) {
        if (isFormSubmit) {
            co_return failWithFormRedirect("该设备登录已完成，请回到客户端");
        }
        co_return TokenError(k400BadRequest, "access_denied", "该设备登录已完成");
    }

    // 浏览器只负责完成授权；token 必须由持有 device_code 的客户端到 /api/oauth2/token 领取。
    std::string notifyUri = session->redirectUri;
    if (notifyUri.empty()) {
        notifyUri = "ueloginreturn://success?state=" + drogon::utils::urlEncodeComponent(userCode);
    }
    auto resp = HttpResponse::newRedirectionResponse(notifyUri);
    co_return resp;
}

Task<HttpResponsePtr> OAuth2Controller::loginDone(HttpRequestPtr req) {
    // 无状态校验：客户端 loopback 收到通知后继续用 device_code 轮询 /api/oauth2/token，
    // 这一页只负责展示结果，不依赖会话。
    auto resp = HttpResponse::newHttpViewResponse("login_done.csp");
    co_return resp;
}

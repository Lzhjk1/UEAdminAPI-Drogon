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
                if (priv >= 2) roles.append("admin");
                else if (priv >= 1) roles.append("user");
                else roles.append("default");
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

/// @brief POST /api/oauth2/login/start — 发起设备登录
/// 入参：{ "redirect_uri": "http://127.0.0.1:PORT/cb" }
/// 返回：{ state, login_url, expires }
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

    std::string state = sessionService->CreateSession(redirectUri);
    int expireSec = sessionService->GetExpireSeconds();

    std::string loginUrl = "/login?state=" + drogon::utils::urlEncodeComponent(state);
    if (!redirectUri.empty()) {
        loginUrl += "&redirect_uri=" + drogon::utils::urlEncodeComponent(redirectUri);
    }

    result.setResult(ApiErrorCode::ApiError_Success);
    result.jsondata["state"] = state;
    result.jsondata["login_url"] = loginUrl;
    result.jsondata["expires"] = expireSec;

    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    co_return resp;
}

/// @brief GET /api/oauth2/login/check?state= — 轮询登录状态
/// 未完成：{active:false}；已完成：{active:true, token, flashToken, username}；取后即废
Task<HttpResponsePtr> OAuth2Controller::loginCheck(HttpRequestPtr req, std::string state) {
    HttpResult result;

    auto sessionService = UEAdminAPI::Services::DeviceLoginSessionService::Instance();
    if (!sessionService) {
        result.setResult(ApiErrorCode::ApiError_InternalError, "DeviceLoginSessionService 未初始化");
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k500InternalServerError);
        co_return resp;
    }

    auto sessionOpt = sessionService->FindSession(state);
    if (!sessionOpt) {
        result.setResult(ApiErrorCode::ApiError_DeviceLoginStateInvalid,
                         "state 不存在、已过期或已消费");
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k404NotFound);
        co_return resp;
    }

    const auto &session = *sessionOpt;
    if (session.userId <= 0) {
        // 尚未登录完成：不消费 state，保证并发轮询均可继续拿到 active:false
        result.setResult(ApiErrorCode::ApiError_Success);
        result.jsondata["active"] = false;
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k200OK);
        co_return resp;
    }

    // 已登录：原子消费会话后生成新 Token 对并返回。
    // 先消费再生成 token，避免两个并发 check 同时通过 FindSession 并各自返回 token。
    auto consumedSession = sessionService->ConsumeSession(state);
    if (!consumedSession) {
        // 另一个并发请求已消费：按“已被领取”语义处理。
        result.setResult(ApiErrorCode::ApiError_DeviceLoginStateConsumed,
                         "state 已被使用，请勿重复获取 token");
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k409Conflict);
        co_return resp;
    }

    // 后续流程如果失败（如用户查询失败），会话已消费不再恢复；客户端可重新发起设备登录。
    auto authService = AuthService::Instance();
    auto [token, flashToken, status] = co_await authService->NewTokenPair(consumedSession->userId);
    if (status == -1) {
        result.setResult(ApiErrorCode::ApiError_UserUpdateFailed, "更新状态失败");
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k500InternalServerError);
        co_return resp;
    }

    std::string username;
    try {
        auto dbClientPtr = drogon::app().getDbClient();
        drogon::orm::Mapper<drogon_model::UEAdminAPI::User> mapper(dbClientPtr);
        auto user = mapper.findByPrimaryKey(consumedSession->userId);
        username = user.getValueOfName();
    } catch (const drogon::orm::UnexpectedRows &) {
        // 用户已被删除：会话已消费，返回明确错误，避免返回空用户名。
        result.setResult(ApiErrorCode::ApiError_UserNotFound, "用户不存在");
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k404NotFound);
        co_return resp;
    } catch (const std::exception &e) {
        LOG_ERROR << "loginCheck: 查询用户失败: " << e.what();
        result.setResult(ApiErrorCode::ApiError_InternalError, "查询用户失败");
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k500InternalServerError);
        co_return resp;
    }

    result.setResult(ApiErrorCode::ApiError_Success);
    result.jsondata["active"] = true;
    result.jsondata["token"] = token;
    result.jsondata["flashToken"] = flashToken;
    result.jsondata["username"] = username;

    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    co_return resp;
}

/// @brief GET /login — 设备登录页（第一阶段：密码登录表单）
Task<HttpResponsePtr> OAuth2Controller::loginPage(HttpRequestPtr req) {
    auto state = req->getParameter("state");
    auto redirectUri = req->getParameter("redirect_uri");

    auto sessionService = UEAdminAPI::Services::DeviceLoginSessionService::Instance();
    if (!sessionService) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("<html><body>服务未初始化</body></html>");
        resp->setContentTypeCode(CT_TEXT_HTML);
        resp->setStatusCode(k500InternalServerError);
        co_return resp;
    }

    auto sessionOpt = sessionService->FindSession(state);
    if (!sessionOpt) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("<html><body><h2>登录会话无效或已过期</h2></body></html>");
        resp->setContentTypeCode(CT_TEXT_HTML);
        resp->setStatusCode(k400BadRequest);
        co_return resp;
    }

    // 视图层做 HTML 转义，防止 GET /login 的 redirect_uri 参数注入 XSS。
    HttpViewData viewData;
    viewData.insertAsString("state", HttpViewData::htmlTranslate(state));
    viewData.insertAsString("redirect_uri", HttpViewData::htmlTranslate(redirectUri));
    auto resp = HttpResponse::newHttpViewResponse("oauth2_login.csp", viewData);
    resp->setStatusCode(k200OK);
    co_return resp;
}

/// @brief POST /login — 设备登录页账号密码登录
/// 第一阶段仅支持用户名密码；成功后写 state→userId 并跳转通知。
Task<HttpResponsePtr> OAuth2Controller::loginByPwd(HttpRequestPtr req) {
    HttpResult result;

    // 兼容两种提交格式：
    // 1) JSON body（设备/脚本调用）
    // 2) 原生 HTML 表单 application/x-www-form-urlencoded（登录页原生表单导航，避免 CORS）
    auto reqJson = req->getJsonObject();
    if (!reqJson) {
        // 尝试从 form 参数读取
        auto params = req->getParameters();
        Json::Value formJson(Json::objectValue);
        if (params.find("state") != params.end())
            formJson["state"] = params["state"];
        if (params.find("userName") != params.end())
            formJson["userName"] = params["userName"];
        if (params.find("passWord") != params.end())
            formJson["passWord"] = params["passWord"];
        if (params.find("redirect_uri") != params.end())
            formJson["redirect_uri"] = params["redirect_uri"];
        if (formJson.isMember("state") && formJson.isMember("userName") &&
            formJson.isMember("passWord")) {
            reqJson = std::make_shared<Json::Value>(formJson);
        }
    }
    if (!reqJson) {
        result.setResult(ApiErrorCode::ApiError_InvalidJsonFormat);
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k400BadRequest);
        co_return resp;
    }

    PostParamMap paramMap;
    paramMap.addParam("state", true)
            .addParam("userName", true)
            .addParam("passWord", true)
            .addParam("redirect_uri", false);
    paramMap.readParamsFromJson(*reqJson);
    auto missing = paramMap.checkRequiredParams();
    if (!missing.empty()) {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "缺少参数: " + missing[0]);
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k400BadRequest);
        co_return resp;
    }

    std::string state = paramMap.getParam("state");
    std::string userName = paramMap.getParam("userName");
    std::string passWord = paramMap.getParam("passWord");
    std::string redirectUri = paramMap.getParam("redirect_uri");

    auto sessionService = UEAdminAPI::Services::DeviceLoginSessionService::Instance();
    if (!sessionService) {
        result.setResult(ApiErrorCode::ApiError_InternalError, "DeviceLoginSessionService 未初始化");
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k500InternalServerError);
        co_return resp;
    }

    auto sessionOpt = sessionService->FindSession(state);
    if (!sessionOpt) {
        result.setResult(ApiErrorCode::ApiError_DeviceLoginStateInvalid, "state 不存在或已过期");
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k400BadRequest);
        co_return resp;
    }

    const auto &session = *sessionOpt;
    if (session.userId > 0) {
        result.setResult(ApiErrorCode::ApiError_DeviceLoginStateConsumed,
                         "该登录会话已完成，请直接使用 login/check 获取 token");
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k400BadRequest);
        co_return resp;
    }

    // 通知地址必须以创建会话时保存的 redirect_uri 为准，防止请求体篡改跳转目标。
    if (redirectUri != session.redirectUri) {
        result.setResult(ApiErrorCode::ApiError_DeviceLoginRedirectUriNotAllowed,
                         "redirect_uri 与登录会话不一致");
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k400BadRequest);
        co_return resp;
    }

    auto authService = AuthService::Instance();
    auto loginResult = co_await authService->LoginByPwd(userName, passWord);
    if (loginResult.code != 0) {
        auto resp = HttpResponse::newHttpJsonResponse(loginResult.toJson());
        resp->setStatusCode(k401Unauthorized);
        co_return resp;
    }

    int userId = loginResult.jsondata["id"].asInt();
    if (!sessionService->MarkLoggedIn(state, userId)) {
        result.setResult(ApiErrorCode::ApiError_DeviceLoginStateConsumed,
                         "该登录会话已完成，请直接使用 login/check 获取 token");
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k400BadRequest);
        co_return resp;
    }

    // 登录成功：通知跳转。使用会话保存的 redirect_uri；无则兼容 ueloginreturn。
    std::string notifyUri = session.redirectUri;
    if (notifyUri.empty()) {
        notifyUri = "ueloginreturn://success?state=" + state;
    }
    auto resp2 = HttpResponse::newRedirectionResponse(notifyUri);
    co_return resp2;
}


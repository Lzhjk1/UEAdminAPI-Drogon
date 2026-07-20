#include "AuthFilter.h"
#include "services/AuthService.h"
#include "utils/DataFormatUtils.h"
#include "utils/HttpResult.h"
#include "utils/ApiErrorCodes.h"
#include <drogon/utils/coroutine.h>

using namespace drogon;

void AuthFilter::doFilter(const HttpRequestPtr &req,
                          FilterCallback &&fcb,
                          FilterChainCallback &&fccb)
{
    auto [authType, token] = UEAdminAPI::DataFormatUtil::parseTokenFromAuthorizationHeader(req->getHeader("Authorization"));
    
    if (token.empty()) {
        bool allowAnonymous = false;
        // 如果是获取 ActionToken 的接口，检查是否为无需登录的操作
        if (req->path() == "/user/action_token") {
            auto mfaTypeStr = req->getParameter("mfaType");
            if (!mfaTypeStr.empty()) {
                eMFAType mfaType = stringToMFAType(mfaTypeStr);
                // 登录和注册相关的 MFA 操作不需要预先登录
                if (mfaType == eMFAType::Register || mfaType == eMFAType::Login || mfaType == eMFAType::LoginOrRegister || mfaType == eMFAType::ResetPassword) {
                    allowAnonymous = true;
                }
            }
        }

        if (allowAnonymous) {
            fccb();
            return;
        }

        auto resp = HttpResponse::newHttpResponse();
        UEAdminAPI::utils::HttpResult result(static_cast<int32_t>(UEAdminAPI::ApiErrorCode::ApiError_TokenMissing), "Authorization in header is missing or empty");
        resp->setBody(result.toJsonString());
        resp->setStatusCode(k200OK);
        fcb(resp);
        return;
    }

    drogon::async_run([req, fcb = std::move(fcb), fccb = std::move(fccb), token]() -> Task<void> {
        auto _authService = AuthService::Instance();
        auto result = co_await _authService->VerifyToken(token);
        
        if (result.code == 0) {
            if (result.jsondata["tokenType"].asString() == "token") {
                // 验证成功，将 userId 注入请求属性中
                req->getAttributes()->insert("userId", result.jsondata["userId"].asInt());
                fccb();
            } 
            // flashtoken
            else if (result.jsondata["tokenType"].asString() == "flashtoken") {
                // 这里不应该使用flashToken, 其只应用于刷新token
                // 提示并返回
                result.setResult(UEAdminAPI::ApiErrorCode::ApiError_AuthenticationFailed, "flashToken 不能直接用于认证");
                auto resp = HttpResponse::newHttpResponse();
                resp->setBody(result.toJsonString());
                resp->setStatusCode(k401Unauthorized);
                fcb(resp);
            } else {
                result.setResult(UEAdminAPI::ApiErrorCode::ApiError_AuthenticationFailed, "未知token类型");
                auto resp = HttpResponse::newHttpResponse();
                resp->setBody(result.toJsonString());
                resp->setStatusCode(k401Unauthorized);
                fcb(resp);
            }
        } else {
            // 验证失败，直接返回错误信息
            result.setResult(UEAdminAPI::ApiErrorCode::ApiError_TokenInvalidOrExpired, "token已失效");
            auto resp = HttpResponse::newHttpResponse();
            resp->setBody(result.toJsonString());
            resp->setStatusCode(k401Unauthorized);
            fcb(resp);
        }
    });
}

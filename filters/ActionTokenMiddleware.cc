#include "ActionTokenMiddleware.h"
#include "services/ActionTokenService.h"
#include "utils/HttpResult.h"
#include "utils/ApiErrorCodes.h"

using namespace drogon;
using namespace UEAdminAPI::Services;
using namespace UEAdminAPI::utils;
using namespace UEAdminAPI;

void ActionTokenMiddleware::invoke(const HttpRequestPtr &req,
                                   MiddlewareNextCallback &&nextCb,
                                   MiddlewareCallback &&mcb)
{
    auto _actionTokenService = ActionTokenService::Instance();

    // 1. 获取当前请求需要的 Action 类别
    eMFAType expectedAction = _actionTokenService->GetActionByRoute(req->path(), req->method());
    if (expectedAction == eMFAType::Error) {
        LOG_FATAL << "ActionTokenMiddleware: 未注册的路由: " << req->method() << ":" << req->path() 
                  << " 应用了 ActionTokenMiddleware, 请检查是否忘记在 ActionTokenService 的构造函数里注册或更改了路由!";
        HttpResult result(static_cast<int32_t>(ApiErrorCode::ApiError_AuthenticationFailed), "认证失败");
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k401Unauthorized);
        mcb(resp);
        return;
    }

    // 2. 从请求头中提取 X-Action-Token
    std::string actionToken = req->getHeader("X-Action-Token");
    if (actionToken.empty()) {
        HttpResult result(static_cast<int32_t>(ApiErrorCode::ApiError_MissingRequiredArgs), "缺少 X-Action-Token 请求头");
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k401Unauthorized);
        mcb(resp);
        return;
    }

    // 3. 提取当前登录用户的 userId
    auto attributes = req->getAttributes();
    int userId = -1;
    if (attributes->find("userId")) {
        userId = attributes->get<int>("userId");
    }

    // 4. 提取请求中的操作目标 (匿名状态下)
    std::string requestTarget;
    if (userId <= 0) {
        const std::string& path = req->path();
        if (path == "/api/user/login/email" || path == "/api/user/create/email") {
            requestTarget = req->getParameter("email");
        } else if (path == "/api/user/login/phone" || path == "/api/user/create/phone") {
            requestTarget = req->getParameter("phone");
        } else if (path == "/api/user/create") {
            auto reqJson = req->getJsonObject();
            if (reqJson) {
                if (reqJson->isMember("email") && (*reqJson)["email"].isString()) {
                    requestTarget = (*reqJson)["email"].asString();
                } else if (reqJson->isMember("phone") && (*reqJson)["phone"].isString()) {
                    requestTarget = (*reqJson)["phone"].asString();
                } else if (reqJson->isMember("tel") && (*reqJson)["tel"].isString()) {
                    requestTarget = (*reqJson)["tel"].asString();
                }
            }
        }
    }

    // 5. 提取 Token (原子操作: 获取并删除，解决并发安全问题)
    auto tokenInfoOpt = _actionTokenService->ExtractToken(actionToken, expectedAction, userId, requestTarget);

    if (!tokenInfoOpt.has_value()) {
        HttpResult result(static_cast<int32_t>(ApiErrorCode::ApiError_InvalidVerifyCode), "ActionToken 无效、已过期、正在处理中或无权执行此操作");
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k200OK);
        mcb(resp);
        return;
    }

    // 6. 执行后续处理
    nextCb([mcb = std::move(mcb), actionToken, info = tokenInfoOpt.value()](const HttpResponsePtr &resp) {
        // 7. 根据执行结果决定是否恢复 Token
        // 只有当业务逻辑执行失败 (code != 0) 时才恢复 Token 供用户重试
        bool shouldRestore = true;
        if (resp->statusCode() == k200OK) {
            auto resJson = resp->getJsonObject();
            if (resJson && resJson->isMember("code") && (*resJson)["code"].asInt() == 0) {
                shouldRestore = false;
            }
        }

        if (shouldRestore) {
            // 业务失败，恢复 Token，允许有限次数的重试
            ActionTokenService::Instance()->RestoreToken(actionToken, info);
        }

        mcb(resp);
    });
}

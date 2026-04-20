#include "ActionTokenFilter.h"
#include "services/ActionTokenService.h"
#include "utils/HttpResult.h"
#include "utils/ApiErrorCodes.h"

using namespace drogon;
using namespace UEAdminAPI::Services;
using namespace UEAdminAPI::utils;
using namespace UEAdminAPI;

ActionTokenFilter::ActionTokenFilter()
{
}

void ActionTokenFilter::doFilter(const HttpRequestPtr &req,
                                 FilterCallback &&fcb,
                                 FilterChainCallback &&fccb)
{
    auto _actionTokenService = ActionTokenService::Instance();

    // 1. 获取当前请求需要的 Action 类别 (直接向 Service 查询)
    eMFAType expectedAction = _actionTokenService->GetActionByRoute(req->path(), req->method());
    if (expectedAction == eMFAType::Error) {
        // 如果当前路由没有注册, 表示有请求路径忘记注册或是路径更改, 拒绝请求并LOG_FATAL
        LOG_FATAL << "ActionTokenFilter: 未注册的路由: "<< req->method() << ":" << req->path() <<" 应用了 ActionTokenFilter, 请检查是否忘记在 ActionTokenService 的构造函数里注册或更改了路由!" << req->path();
        auto resp = HttpResponse::newHttpResponse();
        HttpResult result(static_cast<int32_t>(ApiErrorCode::ApiError_AuthenticationFailed), "认证失败");
        resp->setBody(result.toJsonString());
        resp->setStatusCode(k401Unauthorized);
        fcb(resp);
        return;
    }

    // 2. 从请求头中提取 X-Action-Token
    std::string actionToken = req->getHeader("X-Action-Token");
    if (actionToken.empty()) {
        auto resp = HttpResponse::newHttpResponse();
        HttpResult result(static_cast<int32_t>(ApiErrorCode::ApiError_MissingRequiredArgs), "缺少 X-Action-Token 请求头");
        resp->setBody(result.toJsonString());
        resp->setStatusCode(k401Unauthorized);
        fcb(resp);
        return;
    }

    // 3. 提取当前登录用户的 userId
    // 登录或注册等无需预先登录的接口可能没有 userId
    auto attributes = req->getAttributes();
    int userId = -1;
    if (attributes->find("userId")) {
        userId = attributes->get<int>("userId");
    }

    // 匿名状态(未登录)下, 提取请求中的操作目标, 用于后续校验给出的ActionToken中包含的操作目标是否匹配
    // 防止A目标的验证码被B目标使用, 或者B目标的验证码被A目标使用
    std::string requestTarget;
    if (userId <= 0) {
        const std::string& path = req->path();
        if (path == "/user/login/email") {
            requestTarget = req->getParameter("email");
        } else if (path == "/user/login/phone" || path == "/user/create/phone") {
            requestTarget = req->getParameter("phone");
        } else if (path == "/user/create") {
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

    bool isValid = _actionTokenService->VerifyAndConsumeToken(actionToken, expectedAction, userId, requestTarget);

    if (isValid) {
        // 校验通过，放行
        fccb();
    } else {
        // 校验失败（Token无效、过期、或者类别不匹配）
        auto resp = HttpResponse::newHttpResponse();
        // 建议添加一个新的错误码表示 ActionToken 无效，这里暂时使用统一的验证错误
        HttpResult result(static_cast<int32_t>(ApiErrorCode::ApiError_InvalidVerifyCode), "ActionToken 无效、已过期或无权执行此操作");
        resp->setBody(result.toJsonString());
        resp->setStatusCode(k200OK);
        fcb(resp);
    }
}

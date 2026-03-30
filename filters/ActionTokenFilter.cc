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
    eMFAType expectedAction = _actionTokenService->GetActionByRoute(req->path());
    if (expectedAction == eMFAType::Error) {
        // 如果当前路由不需要 ActionToken，直接放行
        fccb();
        return;
    }

    // 2. 从请求头中提取 X-Action-Token
    std::string actionToken = req->getHeader("X-Action-Token");
    if (actionToken.empty()) {
        auto resp = HttpResponse::newHttpResponse();
        HttpResult result(static_cast<int32_t>(ApiErrorCode::ApiError_MissingRequiredArgs), "缺少 X-Action-Token 请求头");
        resp->setBody(result.toJsonString());
        resp->setStatusCode(k200OK);
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

    // 4. 校验并消耗 Token
    bool isValid = _actionTokenService->VerifyAndConsumeToken(actionToken, expectedAction, userId);

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
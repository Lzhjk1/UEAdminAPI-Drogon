#include "RateLimitFilter.h"
#include "utils/HttpResult.h"
#include "utils/ApiErrorCodes.h"
#include <drogon/HttpResponse.h>

using namespace drogon;
using namespace UEAdminAPI::utils;
using namespace UEAdminAPI;

RateLimitFilter::RateLimitFilter()
{
    // 初始化 CacheMap，每 5 秒清理一次过期项
    _requestCountCache = std::make_unique<drogon::CacheMap<std::string, RateLimitRecord>>(
        drogon::app().getLoop(), 
        5.0
    );

    auto config = drogon::app().getCustomConfig();
    if (config.isMember("RateLimit")) {
        _maxRequests = config["RateLimit"].get("MaxRequests", 60).asInt();
        _timeWindowSeconds = config["RateLimit"].get("TimeWindowSeconds", 60).asInt();
    } else {
        _maxRequests = 60; // 默认每个时间窗口 60 次
        _timeWindowSeconds = 60; // 默认时间窗口为 60 秒
    }
}

void RateLimitFilter::doFilter(const HttpRequestPtr &req,
                               FilterCallback &&fcb,
                               FilterChainCallback &&fccb)
{
    // 标识用户: 优先使用 userId, 如果没有则使用 IP
    std::string clientId = req->peerAddr().toIp();
    auto attributes = req->getAttributes();
    if (attributes->find("userId")) {
        clientId += "-" + std::to_string(attributes->get<int>("userId"));
    }
    
    // 加上请求路径，实现对不同接口独立限流
    clientId += "-" + req->path();

    auto now = std::chrono::steady_clock::now();
    bool isBlocked = false;

    {
        std::lock_guard<std::mutex> lock(_mutex);
        RateLimitRecord record;
        bool found = _requestCountCache->findAndFetch(clientId, record);

        if (found) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - record.startTime).count();
            if (elapsed > _timeWindowSeconds) {
                // 已超过时间窗口，重置计数和时间
                record.count = 1;
                record.startTime = now;
            } else {
                record.count++;
                if (record.count > _maxRequests) {
                    isBlocked = true;
                }
            }
        } else {
            // 首次访问
            record.count = 1;
            record.startTime = now;
        }

        // 写回缓存，超时时间设置为窗口时间的两倍，确保过期后能被自动清理
        if (!isBlocked) {
            _requestCountCache->insert(clientId, record, _timeWindowSeconds * 2);
        }
    }

    if (isBlocked) {
        auto resp = HttpResponse::newHttpResponse();
        HttpResult result(static_cast<int32_t>(ApiErrorCode::ApiError_TooManyRequests), "访问过于频繁，请稍后再试");
        resp->setBody(result.toJsonString());
        resp->setStatusCode(k200OK);
        fcb(resp);
        return;
    }

    // 校验通过，放行
    fccb();
}

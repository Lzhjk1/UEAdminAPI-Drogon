#pragma once

#include <drogon/drogon.h>
#include <drogon/HttpFilter.h>
#include <drogon/CacheMap.h>
#include <string>
#include <memory>
#include <chrono>
#include <mutex>

struct RateLimitRecord {
    int count = 0;
    std::chrono::steady_clock::time_point startTime;
};

class RateLimitFilter : public drogon::HttpFilter<RateLimitFilter>
{
public:
    RateLimitFilter();

    void doFilter(const drogon::HttpRequestPtr &req,
                  drogon::FilterCallback &&fcb,
                  drogon::FilterChainCallback &&fccb) override;

private:
    std::unique_ptr<drogon::CacheMap<std::string, RateLimitRecord>> _requestCountCache;
    int _maxRequests;
    int _timeWindowSeconds;
    std::mutex _mutex; // 保护对缓存中计数记录的并发读写
};

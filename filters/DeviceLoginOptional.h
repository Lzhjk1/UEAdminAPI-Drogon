#pragma once

#include <drogon/HttpFilter.h>

using namespace drogon;

/// @brief 可选设备登录 state 校验过滤器。
/// 当请求带 ueadmin_state 时校验它必须是有效的 OAuth2 设备登录会话；
/// 不带时直接放行（兼容旧客户端 /api/third/authorization_url 调用）。
class DeviceLoginOptional : public HttpFilter<DeviceLoginOptional>
{
public:
    void doFilter(const HttpRequestPtr &req,
                  FilterCallback &&fcb,
                  FilterChainCallback &&fccb) override;
};

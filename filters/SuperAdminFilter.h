#pragma once

#include <drogon/HttpFilter.h>

// 厂商超级管理员校验：必须在 AuthFilter 之后使用（依赖其注入的 userId 属性）。
// 退款审核/查看只认超级管理员（Admin 位或系统角色 super_admin），团队管理员不放行。
class SuperAdminFilter : public drogon::HttpFilter<SuperAdminFilter>
{
public:
    void doFilter(const drogon::HttpRequestPtr &req,
                  drogon::FilterCallback &&fcb,
                  drogon::FilterChainCallback &&fccb) override;
};

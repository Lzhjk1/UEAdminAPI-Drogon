#pragma once

#include <drogon/HttpFilter.h>

// 厂商超管校验：必须在 AuthFilter 之后使用（依赖其注入的 userId 属性）
// 判据：user.privilege 的 Admin 位（utils/EnumUserPrivileges.h，Admin = 1 << 2）
class AdminFilter : public drogon::HttpFilter<AdminFilter>
{
public:
    void doFilter(const drogon::HttpRequestPtr &req,
                  drogon::FilterCallback &&fcb,
                  drogon::FilterChainCallback &&fccb) override;
};

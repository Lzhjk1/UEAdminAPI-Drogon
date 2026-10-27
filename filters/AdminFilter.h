#pragma once

#include <drogon/HttpFilter.h>

// 厂商超管校验：必须在 AuthFilter 之后使用（依赖其注入的 userId 属性）
// 判据（满足其一即可）：
//   1. user.privilege 的 Admin 位（utils/EnumUserPrivileges.h，Admin = 1 << 2）；
//   2. 系统角色 super_admin（role.scope = 1，经 user_system_role 授予）。
class AdminFilter : public drogon::HttpFilter<AdminFilter>
{
public:
    void doFilter(const drogon::HttpRequestPtr &req,
                  drogon::FilterCallback &&fcb,
                  drogon::FilterChainCallback &&fccb) override;
};

#pragma once

#include <drogon/HttpFilter.h>

// 厂商控制台校验（反馈 / 安装包 / 资源市场）：必须在 AuthFilter 之后使用（依赖其注入的 userId 属性）。
// 判据（满足其一即可）：
//   1. 超级管理员（Admin 位或系统角色 super_admin，与 AdminFilter 口径一致）——保持既有权限；
//   2. 厂商团队（team.is_vendor = true）里的团队管理员——客户端「厂商管理」入口的可见对象。
class VendorConsoleFilter : public drogon::HttpFilter<VendorConsoleFilter>
{
public:
    void doFilter(const drogon::HttpRequestPtr &req,
                  drogon::FilterCallback &&fcb,
                  drogon::FilterChainCallback &&fccb) override;
};

#pragma once

#include <string>
#include <vector>
#include "utils/HttpResult.h"

namespace UEAdminAPI {
namespace Services {

// 厂商后台 RBAC 配置：角色 / 权限 / 权限组
// 说明：
//   - 角色 code 与权限 code 创建后不可改（客户端与服务端鉴权依赖 code）；
//   - 团队管理员/工程师/项目经理三个内置角色不允许删除；
//   - 仍被引用的权限/角色不允许删除（避免权限语义悬空）。
class RbacService
{
public:
    // 角色
    static utils::HttpResult ListRoles();
    static utils::HttpResult CreateRole(const std::string &code, const std::string &name, int scope);
    // replacePermissions=false 时只改名字，忽略 permissionIds
    static utils::HttpResult UpdateRole(int roleId, const std::string &name,
                                        const std::vector<int> &permissionIds, bool replacePermissions);
    static utils::HttpResult DeleteRole(int roleId);

    // 权限
    static utils::HttpResult ListPermissions();
    static utils::HttpResult CreatePermission(const std::string &code,
                                              const std::string &module,
                                              const std::string &action);
    static utils::HttpResult UpdatePermission(int permissionId,
                                              const std::string &module,
                                              const std::string &action);
    static utils::HttpResult DeletePermission(int permissionId);

    // 权限组
    static utils::HttpResult ListGroups();
    static utils::HttpResult CreateGroup(const std::string &name, const std::vector<int> &permissionIds);
    static utils::HttpResult UpdateGroup(int groupId, const std::string &name,
                                         const std::vector<int> &permissionIds, bool replacePermissions);
    static utils::HttpResult DeleteGroup(int groupId);
};

} // namespace Services
} // namespace UEAdminAPI

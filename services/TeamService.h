#pragma once

#include <string>
#include <vector>
#include "utils/HttpResult.h"

namespace UEAdminAPI {
namespace Services {

// 团队 / 付费 / 授权 / 余额聚合。全部无状态（每次调用取 app().getDbClient()），
// 因此不需要 SingletonWithInit，也不需要改 main.cc。
class TeamService
{
public:
    // 账号中心聚合：余额 + 团队 + 付费期 + 角色 + 云端功能授权 + 首改密标记
    static utils::HttpResult GetBootstrap(int userId);

    // 我的团队：团队信息 + 付费期 + 成员列表
    static utils::HttpResult GetTeam(int userId);

    // 团队管理员建号（初始密码，首次登录强制改密）
    // roleCodes：scope=2 的团队角色，可多个；team_admin 仅厂商超管/团队所有者可授予
    static utils::HttpResult CreateMember(int operatorUserId,
                                          const std::string &userName,
                                          const std::string &password,
                                          const std::vector<std::string> &roleCodes,
                                          const std::vector<std::string> &systemRoleCodes,
                                          const std::string &nickName);

    // 团队管理员改成员角色（替换式）。团队所有者必须保留 team_admin；
    // 授予 team_admin 同样仅限厂商超管/团队所有者。
    static utils::HttpResult UpdateMemberRoles(int operatorUserId,
                                               int memberUserId,
                                               const std::vector<std::string> &roleCodes,
                                               const std::vector<std::string> &systemRoleCodes,
                                               bool updateSystemRoles);

    // 注册成功后开通个人团队（该用户为团队管理员）。失败返回 false，由调用方回滚。
    static bool ProvisionPersonalTeam(int userId, const std::string &userName);

    // 厂商：团队列表（paidOnly / expiredOnly 二选一，均为 false 表示全部）
    static utils::HttpResult AdminListTeams(bool paidOnly, bool expiredOnly);

    // 厂商：设置团队付费期（新增一条订阅记录，保留历史）
    static utils::HttpResult AdminSetSubscription(int adminUserId,
                                                  int teamId,
                                                  const std::string &startDate,
                                                  const std::string &endDate,
                                                  const std::string &plan);

    // 厂商：设置团队/账号的云端功能授权（team 级）
    static utils::HttpResult AdminSetFeatureAuth(int adminUserId,
                                                 int teamId,
                                                 const std::string &featureCode,
                                                 bool enabled);

    // ------------------------------------------------------------ 超级管理员（跨团队）
    // 以下接口由 AdminFilter 保护：Admin 位或系统角色 super_admin 均可调用。

    // 任意团队的详情：团队资料 + 付费期 + 功能授权 + 成员列表
    static utils::HttpResult AdminGetTeam(int teamId);

    // 任意团队改名 / 启停（name 为空表示不改名，status 为 0 表示不改状态）
    static utils::HttpResult AdminUpdateTeam(int adminUserId,
                                             int teamId,
                                             const std::string &name,
                                             int status);

    // 任意团队的成员角色 / 启停（roleCode 为空表示不改角色，status 为 0 表示不改状态）
    // 团队创建者不允许被停用或降级，避免团队失去唯一管理员。
    static utils::HttpResult AdminUpdateMember(int adminUserId,
                                               int teamId,
                                               int memberUserId,
                                               const std::string &roleCode,
                                               int status);

    // 账号检索（按用户名/昵称模糊匹配），带所属团队与是否超级管理员，供授予角色时定位账号
    static utils::HttpResult AdminListUsers(const std::string &keyword, int limit);

    // 授予/回收系统角色（目前仅 super_admin）。仅 Admin 位的厂商账号可操作，
    // 避免拿到角色的账号自行扩散。
    static utils::HttpResult AdminSetSystemRole(int operatorUserId,
                                                int targetUserId,
                                                const std::string &roleCode,
                                                bool enabled);

    // ------------------------------------------------------------ 厂商团队管理员（跨团队）
    // 操作者必须是「厂商团队的团队管理员」或超级管理员；服务层内部校验。

    // 树形团队管理：全部团队（含成员数、是否厂商/个人团队）
    static utils::HttpResult VendorListTeams(int operatorUserId);

    // 任意团队详情（团队资料 + 付费期 + 成员）
    static utils::HttpResult VendorGetTeam(int operatorUserId, int teamId);

    // 在指定团队新增成员
    static utils::HttpResult VendorCreateMember(int operatorUserId,
                                                int teamId,
                                                const std::string &userName,
                                                const std::string &password,
                                                const std::vector<std::string> &roleCodes,
                                                const std::vector<std::string> &systemRoleCodes,
                                                const std::string &nickName);

    // 修改指定团队成员的团队角色 / 系统角色（替换式）
    static utils::HttpResult VendorUpdateMemberRoles(int operatorUserId,
                                                     int teamId,
                                                     int memberUserId,
                                                     const std::vector<std::string> &roleCodes,
                                                     const std::vector<std::string> &systemRoleCodes,
                                                     bool updateSystemRoles);

    // 把成员移出团队：落回其个人团队（不存在则创建），角色重置为团队管理员
    static utils::HttpResult VendorRemoveMember(int operatorUserId, int teamId, int memberUserId);

    // 本团队管理员移除成员（与上一接口共用同一套保护规则）
    static utils::HttpResult RemoveMember(int operatorUserId, int memberUserId);

    // 跨团队移动：把 userIds 从一个团队移到指定团队，角色统一替换为 roleCodes
    static utils::HttpResult VendorMoveMembers(int operatorUserId,
                                               int teamId,
                                               const std::vector<int> &userIds,
                                               const std::vector<std::string> &roleCodes);

    // 可移动用户检索：不在 excludeTeamId 的用户（按用户名/昵称模糊匹配）
    static utils::HttpResult VendorListCandidates(int operatorUserId,
                                                  const std::string &keyword,
                                                  int excludeTeamId,
                                                  int limit);

    // 团队管理员改自己所在团队的名称
    static utils::HttpResult RenameTeam(int operatorUserId, const std::string &name);

    // 在指定团队建号（权限由调用方校验；授予 team_admin / super_admin 的规则在内）
    static utils::HttpResult CreateMemberInTeam(int operatorUserId,
                                                int teamId,
                                                const std::string &userName,
                                                const std::string &password,
                                                const std::vector<std::string> &roleCodes,
                                                const std::vector<std::string> &systemRoleCodes,
                                                const std::string &nickName);

    // 修改指定团队成员的团队角色（权限由调用方校验）
    static utils::HttpResult UpdateMemberRolesInTeam(int operatorUserId,
                                                     int teamId,
                                                     int memberUserId,
                                                     const std::vector<std::string> &roleCodes,
                                                     const std::vector<std::string> &systemRoleCodes,
                                                     bool updateSystemRoles);

    // 从指定团队移除成员（权限由调用方校验；owner/自己/最后管理员等保护在内）
    static utils::HttpResult RemoveMemberInTeam(int operatorUserId, int teamId, int memberUserId);
};

} // namespace Services
} // namespace UEAdminAPI

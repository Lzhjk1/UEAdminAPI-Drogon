#pragma once

#include <string>
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
    static utils::HttpResult CreateMember(int operatorUserId,
                                          const std::string &userName,
                                          const std::string &password,
                                          const std::string &roleCode,
                                          const std::string &nickName);

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
};

} // namespace Services
} // namespace UEAdminAPI

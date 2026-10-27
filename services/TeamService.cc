#include "TeamService.h"

#include <drogon/drogon.h>
#include "models/User.h"

#include "services/AuthService.h"
#include "utils/ApiErrorCodes.h"
#include "utils/EnumUserPrivileges.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <vector>

using namespace drogon;
using namespace UEAdminAPI;
using namespace UEAdminAPI::utils;

namespace UEAdminAPI {
namespace Services {

namespace {

// 取当前用户所在团队与角色；不在团队时返回 false
bool GetMembership(const std::shared_ptr<drogon::orm::DbClient> &db, int userId, int &teamId, std::string &roleCode)
{
    auto rows = db->execSqlSync(
        "select tm.team_id, r.code as role_code "
        "from team_member tm join role r on r.id = tm.role_id "
        "where tm.user_id = $1 and tm.status = 1",
        userId);
    if (rows.empty())
    {
        return false;
    }
    teamId = rows[0]["team_id"].as<int>();
    roleCode = rows[0]["role_code"].as<std::string>();
    return true;
}

// 厂商超管：Admin 位或系统角色 super_admin（与 AdminFilter 口径一致）
bool IsSuperAdmin(const std::shared_ptr<drogon::orm::DbClient> &db, int userId)
{
    auto rows = db->execSqlSync(
        "select ((coalesce(u.privilege, 0) & $2::int) <> 0 "
        "        or exists(select 1 from user_system_role usr "
        "                   join role sr on sr.id = usr.role_id "
        "                   where usr.user_id = u.id and sr.code = 'super_admin')) as is_super_admin "
        "from \"user\" u where u.id = $1",
        userId, static_cast<int>(UserPrivileges::Admin));
    return !rows.empty() && rows[0]["is_super_admin"].as<bool>();
}

// 谁能把别人设成团队管理员：厂商超管，或该团队的 owner
bool CanGrantTeamAdmin(const std::shared_ptr<drogon::orm::DbClient> &db, int operatorUserId, int teamId)
{
    auto rows = db->execSqlSync("select owner_user_id from team where id = $1", teamId);
    if (!rows.empty() && !rows[0]["owner_user_id"].isNull() &&
        rows[0]["owner_user_id"].as<int>() == operatorUserId)
    {
        return true;
    }
    return IsSuperAdmin(db, operatorUserId);
}

bool HasTeamAdminRole(const std::vector<std::string> &roleCodes)
{
    return std::find(roleCodes.begin(), roleCodes.end(), "team_admin") != roleCodes.end();
}

// 把角色 code 解析成 id（只认 scope=2 的团队角色），去重；team_admin 排最前作为主角色，
// 因为旧版界面只读 team_member.role_id 判断管理员。
bool ResolveTeamRoleIds(const std::shared_ptr<drogon::orm::DbClient> &db,
                        const std::vector<std::string> &roleCodes,
                        std::vector<int> &roleIds)
{
    roleIds.clear();
    if (roleCodes.empty())
    {
        return false;
    }

    std::vector<int> adminRoleIds;
    for (const auto &code : roleCodes)
    {
        auto rows = db->execSqlSync("select id from role where code = $1 and scope = 2", code);
        if (rows.empty())
        {
            return false;
        }
        const int roleId = rows[0]["id"].as<int>();
        std::vector<int> &target = (code == "team_admin") ? adminRoleIds : roleIds;
        if (std::find(roleIds.begin(), roleIds.end(), roleId) == roleIds.end() &&
            std::find(adminRoleIds.begin(), adminRoleIds.end(), roleId) == adminRoleIds.end())
        {
            target.push_back(roleId);
        }
    }
    roleIds.insert(roleIds.begin(), adminRoleIds.begin(), adminRoleIds.end());
    return !roleIds.empty();
}

// 团队详情与厂商后台共用：付费期（取 end_date 最大的一条有效订阅）
Json::Value LoadSubscription(const std::shared_ptr<drogon::orm::DbClient> &db, int teamId)
{
    Json::Value sub;
    auto rows = db->execSqlSync(
        "select to_char(s.start_date, 'YYYY-MM-DD') as sub_start, "
        "       to_char(s.end_date, 'YYYY-MM-DD') as sub_end, "
        "       (s.end_date >= current_date) as paid "
        "from team_subscription s "
        "where s.team_id = $1 and s.status = 1 "
        "order by s.end_date desc limit 1",
        teamId);
    if (!rows.empty())
    {
        sub["startDate"] = rows[0]["sub_start"].as<std::string>();
        sub["endDate"] = rows[0]["sub_end"].as<std::string>();
        sub["active"] = rows[0]["paid"].as<bool>();
    }
    else
    {
        sub["startDate"] = "";
        sub["endDate"] = "";
        sub["active"] = false;
    }
    return sub;
}

// 团队级功能授权（缺失即关闭）；账号级覆盖只在 GetBootstrap 里叠加
Json::Value LoadFeatureAuth(const std::shared_ptr<drogon::orm::DbClient> &db, int teamId)
{
    bool cloudAuth = false;
    bool updateAuth = false;
    auto rows = db->execSqlSync(
        "select f.feature_code, coalesce(tf.enabled, false) as enabled "
        "from (values ('cloud_project'), ('online_update')) as f(feature_code) "
        "left join team_feature_auth tf on tf.team_id = $1 and tf.feature_code = f.feature_code",
        teamId);
    for (const auto &r : rows)
    {
        const std::string code = r["feature_code"].as<std::string>();
        const bool enabled = r["enabled"].as<bool>();
        if (code == "cloud_project")
        {
            cloudAuth = enabled;
        }
        else if (code == "online_update")
        {
            updateAuth = enabled;
        }
    }
    Json::Value auth;
    auth["cloud_project"] = cloudAuth;
    auth["online_update"] = updateAuth;
    return auth;
}

Json::Value LoadTeamMembers(const std::shared_ptr<drogon::orm::DbClient> &db, int teamId)
{
    auto rows = db->execSqlSync(
        "select tm.id as member_id, u.id, u.name, u.nick_name, r.code as role_code, r.name as role_name, "
        "       tm.status, u.must_change_pwd, "
        "       to_char(tm.created_at, 'YYYY-MM-DD HH24:MI:SS') as created_at "
        "from team_member tm "
        "join \"user\" u on u.id = tm.user_id "
        "join role r on r.id = tm.role_id "
        "where tm.team_id = $1 "
        "order by tm.id",
        teamId);

    // 多角色一次取全，避免逐个成员查库
    auto roleRows = db->execSqlSync(
        "select tmr.team_member_id as member_id, r.code as code, r.name as name "
        "from team_member_role tmr "
        "join role r on r.id = tmr.role_id "
        "join team_member tm on tm.id = tmr.team_member_id "
        "where tm.team_id = $1 "
        "order by r.id",
        teamId);
    std::map<int, Json::Value> rolesByMember;
    for (const auto &rr : roleRows)
    {
        Json::Value role;
        role["code"] = rr["code"].as<std::string>();
        role["name"] = rr["name"].as<std::string>();
        rolesByMember[rr["member_id"].as<int>()].append(role);
    }

    Json::Value members(Json::arrayValue);
    for (const auto &r : rows)
    {
        Json::Value m;
        m["userId"] = r["id"].as<int>();
        m["userName"] = r["name"].as<std::string>();
        m["nickName"] = r["nick_name"].as<std::string>();
        m["roleCode"] = r["role_code"].as<std::string>();
        m["roleName"] = r["role_name"].as<std::string>();
        m["status"] = r["status"].as<int>();
        m["mustChangePwd"] = r["must_change_pwd"].as<bool>();
        m["createdAt"] = r["created_at"].as<std::string>();

        const int memberId = r["member_id"].as<int>();
        auto it = rolesByMember.find(memberId);
        if (it != rolesByMember.end())
        {
            m["roles"] = it->second;
        }
        else
        {
            // 迁移未执行/未回填时退回主角色，界面不至于显示空白
            Json::Value role;
            role["code"] = r["role_code"].as<std::string>();
            role["name"] = r["role_name"].as<std::string>();
            m["roles"].append(role);
        }
        members.append(m);
    }
    return members;
}

} // namespace

HttpResult TeamService::GetBootstrap(int userId)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync(
            "select u.id, u.privilege, u.must_change_pwd, "
            "       ((coalesce(u.privilege, 0) & $2::int) <> 0 "
            "        or exists(select 1 from user_system_role usr "
            "                   join role sr on sr.id = usr.role_id "
            "                   where usr.user_id = u.id and sr.code = 'super_admin')) as is_super_admin, "
            "       coalesce(b.balance_cents, 0) as balance_cents, "
            "       t.id as team_id, t.name as team_name, t.status as team_status, r.code as role_code, "
            "       to_char(s.start_date, 'YYYY-MM-DD') as sub_start, "
            "       to_char(s.end_date, 'YYYY-MM-DD') as sub_end, "
            "       (s.end_date is not null and s.end_date >= current_date) as paid "
            "from \"user\" u "
            "left join user_balance b on b.user_id = u.id "
            "left join team_member tm on tm.user_id = u.id and tm.status = 1 "
            "left join team t on t.id = tm.team_id "
            "left join role r on r.id = tm.role_id "
            "left join lateral (select start_date, end_date from team_subscription ts "
            "                   where ts.team_id = t.id and ts.status = 1 "
            "                   order by ts.end_date desc limit 1) s on true "
            "where u.id = $1",
            userId, static_cast<int>(UserPrivileges::Admin));

        if (rows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_UserNotFound);
            return result;
        }

        const auto &row = rows[0];
        result.jsondata["userId"] = row["id"].as<int>();
        result.jsondata["privilege"] = row["privilege"].as<int>();
        result.jsondata["isSuperAdmin"] = row["is_super_admin"].as<bool>();
        result.jsondata["mustChangePwd"] = row["must_change_pwd"].as<bool>();
        result.jsondata["balanceCents"] =
            static_cast<Json::Int64>(row["balance_cents"].as<int64_t>());

        bool teamActive = false;
        int teamId = 0;
        if (!row["team_id"].isNull())
        {
            teamId = row["team_id"].as<int>();
            teamActive = !row["team_status"].isNull() && row["team_status"].as<int>() == 1;
            result.jsondata["team"]["id"] = teamId;
            result.jsondata["team"]["name"] = row["team_name"].as<std::string>();
            result.jsondata["team"]["status"] = teamActive ? 1 : 2;
            result.jsondata["team"]["roleCode"] = row["role_code"].as<std::string>();
        }

        result.jsondata["subscription"]["startDate"] =
            row["sub_start"].isNull() ? "" : row["sub_start"].as<std::string>();
        result.jsondata["subscription"]["endDate"] =
            row["sub_end"].isNull() ? "" : row["sub_end"].as<std::string>();
        result.jsondata["subscription"]["active"] =
            !row["paid"].isNull() && row["paid"].as<bool>();

        // 功能授权：账号级覆盖团队级，缺失视为关闭；团队停用时一律关闭
        bool cloudAuth = false;
        bool updateAuth = false;
        auto authRows = db->execSqlSync(
            "select f.feature_code, coalesce(uf.enabled, tf.enabled, false) as enabled "
            "from (values ('cloud_project'), ('online_update')) as f(feature_code) "
            "left join team_feature_auth tf on tf.team_id = $1 and tf.feature_code = f.feature_code "
            "left join user_feature_auth uf on uf.user_id = $2 and uf.feature_code = f.feature_code",
            teamId, userId);
        for (const auto &r : authRows)
        {
            const std::string code = r["feature_code"].as<std::string>();
            const bool enabled = teamActive && r["enabled"].as<bool>();
            if (code == "cloud_project")
            {
                cloudAuth = enabled;
            }
            else if (code == "online_update")
            {
                updateAuth = enabled;
            }
        }
        result.jsondata["featureAuth"]["cloud_project"] = cloudAuth;
        result.jsondata["featureAuth"]["online_update"] = updateAuth;

        auto versionRows = db->execSqlSync(
            "select version from installer_version where status = 1 "
            "order by published_at desc nulls last, id desc limit 1");
        result.jsondata["latestVersion"] =
            versionRows.empty() ? "" : versionRows[0]["version"].as<std::string>();

        result.setResult(ApiErrorCode::ApiError_Success, "success");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "GetBootstrap 查询失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult TeamService::GetTeam(int userId)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        int teamId = 0;
        std::string roleCode;
        if (!GetMembership(db, userId, teamId, roleCode))
        {
            result.setResult(ApiErrorCode::ApiError_NotTeamMember);
            return result;
        }

        auto teamRows = db->execSqlSync(
            "select id, name, status, owner_user_id from team where id = $1", teamId);
        if (teamRows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_TeamNotFound);
            return result;
        }

        result.jsondata["team"]["id"] = teamId;
        result.jsondata["team"]["name"] = teamRows[0]["name"].as<std::string>();
        result.jsondata["team"]["status"] = teamRows[0]["status"].as<int>();
        result.jsondata["team"]["roleCode"] = roleCode;
        result.jsondata["team"]["ownerUserId"] =
            teamRows[0]["owner_user_id"].isNull() ? 0 : teamRows[0]["owner_user_id"].as<int>();
        // 客户端据此决定是否把「团队管理员」作为可勾选角色
        result.jsondata["canGrantTeamAdmin"] = CanGrantTeamAdmin(db, userId, teamId);

        result.jsondata["subscription"] = LoadSubscription(db, teamId);
        result.jsondata["members"] = LoadTeamMembers(db, teamId);

        result.setResult(ApiErrorCode::ApiError_Success, "success");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "GetTeam 查询失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult TeamService::CreateMember(int operatorUserId,
                                     const std::string &userName,
                                     const std::string &password,
                                     const std::vector<std::string> &roleCodes,
                                     const std::string &nickName)
{
    HttpResult result;
    if (userName.empty() || password.size() < 6)
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "用户名不能为空，初始密码至少 6 位");
        return result;
    }
    if (roleCodes.empty())
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "至少选择一个角色");
        return result;
    }

    auto db = drogon::app().getDbClient();
    try
    {
        int teamId = 0;
        std::string operatorRole;
        if (!GetMembership(db, operatorUserId, teamId, operatorRole))
        {
            result.setResult(ApiErrorCode::ApiError_NotTeamMember);
            return result;
        }
        if (operatorRole != "team_admin")
        {
            result.setResult(ApiErrorCode::ApiError_NotTeamAdmin);
            return result;
        }

        auto teamRows = db->execSqlSync("select status from team where id = $1", teamId);
        if (teamRows.empty() || teamRows[0]["status"].as<int>() != 1)
        {
            result.setResult(ApiErrorCode::ApiError_InvalidOperation, "团队已停用，不能新建成员");
            return result;
        }

        std::vector<int> roleIds;
        if (!ResolveTeamRoleIds(db, roleCodes, roleIds))
        {
            result.setResult(ApiErrorCode::ApiError_InvalidRoleCode);
            return result;
        }
        // 团队管理员属于管理岗：只有厂商超管/团队所有者能授予，避免团队内自我扩权
        if (HasTeamAdminRole(roleCodes) && !CanGrantTeamAdmin(db, operatorUserId, teamId))
        {
            result.setResult(ApiErrorCode::ApiError_NotTeamAdmin,
                             "只有厂商超级管理员或团队所有者可以授予团队管理员");
            return result;
        }

        auto existRows = db->execSqlSync("select 1 from \"user\" where name = $1", userName);
        if (!existRows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_UsernameAlreadyExists);
            return result;
        }

        auto authService = AuthService::Instance();
        auto [passwordHash, passwordSalt] = authService->CreateStrPasswordHash(password);

        drogon::orm::Mapper<drogon_model::UEAdminAPI::User> mapperUser(db);
        drogon_model::UEAdminAPI::User newUser;
        newUser.setName(userName);
        newUser.setPasswordHash(passwordHash);
        newUser.setPasswordSalt(passwordSalt);
        newUser.setPrivilege(static_cast<int>(UserPrivileges::Default) |
                             static_cast<int>(UserPrivileges::User));
        newUser.setNickName(nickName.empty() ? userName : nickName);
        newUser.setTelephoneNumber("");
        newUser.setEmail("");
        newUser.setIsMale(true);
        newUser.setCreateAt(trantor::Date::now());
        mapperUser.insert(newUser);

        const int newUserId = newUser.getValueOfId();
        try
        {
            db->execSqlSync("update \"user\" set must_change_pwd = true where id = $1", newUserId);
            auto inserted = db->execSqlSync(
                "insert into team_member (team_id, user_id, role_id, status) values ($1, $2, $3, 1) "
                "returning id",
                teamId, newUserId, roleIds[0]);
            const int memberId = inserted[0]["id"].as<int>();
            for (const int roleId : roleIds)
            {
                db->execSqlSync(
                    "insert into team_member_role (team_member_id, role_id) values ($1, $2) "
                    "on conflict do nothing",
                    memberId, roleId);
            }
            db->execSqlSync("insert into user_balance (user_id) values ($1) on conflict do nothing",
                            newUserId);
        }
        catch (const drogon::orm::DrogonDbException &e)
        {
            LOG_ERROR << "创建团队成员后续写入失败, 回滚用户: " << e.base().what();
            try
            {
                mapperUser.deleteOne(newUser);
            }
            catch (...)
            {
                LOG_ERROR << "回滚删除用户失败, userId=" << newUserId;
            }
            result.setResult(ApiErrorCode::ApiError_TeamMemberCreateFailure);
            return result;
        }

        result.jsondata["userId"] = newUserId;
        result.setResult(ApiErrorCode::ApiError_Success, "创建成员成功");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "CreateMember 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_TeamMemberCreateFailure);
    }
    return result;
}

HttpResult TeamService::UpdateMemberRoles(int operatorUserId,
                                          int memberUserId,
                                          const std::vector<std::string> &roleCodes)
{
    HttpResult result;
    if (roleCodes.empty())
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "至少保留一个角色");
        return result;
    }

    auto db = drogon::app().getDbClient();
    try
    {
        int teamId = 0;
        std::string operatorRole;
        if (!GetMembership(db, operatorUserId, teamId, operatorRole))
        {
            result.setResult(ApiErrorCode::ApiError_NotTeamMember);
            return result;
        }
        if (operatorRole != "team_admin")
        {
            result.setResult(ApiErrorCode::ApiError_NotTeamAdmin);
            return result;
        }

        auto memberRows = db->execSqlSync(
            "select id from team_member where user_id = $1 and team_id = $2",
            memberUserId, teamId);
        if (memberRows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_NotTeamMember, "该账号不是本团队成员");
            return result;
        }
        const int memberId = memberRows[0]["id"].as<int>();

        std::vector<int> roleIds;
        if (!ResolveTeamRoleIds(db, roleCodes, roleIds))
        {
            result.setResult(ApiErrorCode::ApiError_InvalidRoleCode);
            return result;
        }

        const bool grantAdmin = HasTeamAdminRole(roleCodes);
        if (grantAdmin && !CanGrantTeamAdmin(db, operatorUserId, teamId))
        {
            result.setResult(ApiErrorCode::ApiError_NotTeamAdmin,
                             "只有厂商超级管理员或团队所有者可以授予团队管理员");
            return result;
        }

        // 所有者必须保留 team_admin：否则团队会失去唯一的管理员
        auto teamRows = db->execSqlSync("select owner_user_id from team where id = $1", teamId);
        const bool isOwner = !teamRows.empty() && !teamRows[0]["owner_user_id"].isNull() &&
                             teamRows[0]["owner_user_id"].as<int>() == memberUserId;
        if (isOwner && !grantAdmin)
        {
            result.setResult(ApiErrorCode::ApiError_TeamOwnerProtected);
            return result;
        }

        // 只做差集增删（不是先清空再插入）：中途失败也不会把成员已有角色清空
        auto existingRows = db->execSqlSync(
            "select role_id from team_member_role where team_member_id = $1", memberId);
        std::vector<int> existingIds;
        for (const auto &row : existingRows)
        {
            existingIds.push_back(row["role_id"].as<int>());
        }

        for (const int roleId : roleIds)
        {
            db->execSqlSync(
                "insert into team_member_role (team_member_id, role_id) values ($1, $2) "
                "on conflict do nothing",
                memberId, roleId);
        }
        for (const int roleId : existingIds)
        {
            if (std::find(roleIds.begin(), roleIds.end(), roleId) == roleIds.end())
            {
                db->execSqlSync(
                    "delete from team_member_role where team_member_id = $1 and role_id = $2",
                    memberId, roleId);
            }
        }
        db->execSqlSync("update team_member set role_id = $2 where id = $1", memberId, roleIds[0]);

        Json::Value roles(Json::arrayValue);
        for (const auto &code : roleCodes)
        {
            roles.append(code);
        }
        result.jsondata["roles"] = roles;
        result.setResult(ApiErrorCode::ApiError_Success, "角色已更新");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "UpdateMemberRoles 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

bool TeamService::ProvisionPersonalTeam(int userId, const std::string &userName)
{
    auto db = drogon::app().getDbClient();
    try
    {
        auto memberRows = db->execSqlSync("select 1 from team_member where user_id = $1", userId);
        if (!memberRows.empty())
        {
            return true;
        }

        auto roleRows = db->execSqlSync("select id from role where code = 'team_admin'");
        if (roleRows.empty())
        {
            LOG_ERROR << "ProvisionPersonalTeam: 缺少 team_admin 角色, 请先执行迁移";
            return false;
        }
        const int roleId = roleRows[0]["id"].as<int>();

        const std::string teamName = "Team-" + std::to_string(userId) + "-" + userName;
        auto teamRows = db->execSqlSync(
            "insert into team (name, owner_user_id, status) values ($1, $2, 1) "
            "on conflict (name) do update set owner_user_id = excluded.owner_user_id "
            "returning id",
            teamName, userId);
        if (teamRows.empty())
        {
            return false;
        }
        const int teamId = teamRows[0]["id"].as<int>();

        db->execSqlSync(
            "insert into team_member (team_id, user_id, role_id, status) values ($1, $2, $3, 1) "
            "on conflict (user_id) do nothing",
            teamId, userId, roleId);
        db->execSqlSync("insert into user_balance (user_id) values ($1) on conflict do nothing",
                        userId);
        return true;
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "ProvisionPersonalTeam 失败: " << e.base().what();
        return false;
    }
}

HttpResult TeamService::AdminListTeams(bool paidOnly, bool expiredOnly)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        std::string having = "";
        if (paidOnly)
        {
            having = " and (s.end_date is not null and s.end_date >= current_date)";
        }
        else if (expiredOnly)
        {
            having = " and (s.end_date is null or s.end_date < current_date)";
        }

        auto rows = db->execSqlSync(
            "select t.id, t.name, t.status, to_char(t.created_at, 'YYYY-MM-DD') as created_at, "
            "       to_char(s.start_date, 'YYYY-MM-DD') as sub_start, "
            "       to_char(s.end_date, 'YYYY-MM-DD') as sub_end, "
            "       (s.end_date is not null and s.end_date >= current_date) as paid, "
            "       (select count(*) from team_member m where m.team_id = t.id and m.status = 1) as member_count "
            "from team t "
            "left join lateral (select start_date, end_date from team_subscription ts "
            "                   where ts.team_id = t.id and ts.status = 1 "
            "                   order by ts.end_date desc limit 1) s on true "
            "where 1 = 1" + having +
            " order by t.id");

        Json::Value teams(Json::arrayValue);
        for (const auto &r : rows)
        {
            Json::Value t;
            t["id"] = r["id"].as<int>();
            t["name"] = r["name"].as<std::string>();
            t["status"] = r["status"].as<int>();
            t["createdAt"] = r["created_at"].as<std::string>();
            t["subscription"]["startDate"] = r["sub_start"].isNull() ? "" : r["sub_start"].as<std::string>();
            t["subscription"]["endDate"] = r["sub_end"].isNull() ? "" : r["sub_end"].as<std::string>();
            t["subscription"]["active"] = !r["paid"].isNull() && r["paid"].as<bool>();
            t["memberCount"] = r["member_count"].as<int>();
            teams.append(t);
        }
        result.jsondata["teams"] = teams;
        result.setResult(ApiErrorCode::ApiError_Success, "success");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "AdminListTeams 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult TeamService::AdminSetSubscription(int adminUserId,
                                             int teamId,
                                             const std::string &startDate,
                                             const std::string &endDate,
                                             const std::string &plan)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();

    // 日期格式与真实日期都交给 PostgreSQL 判定，失败按业务错误返回（而不是 -104 数据库错误）
    auto isValidDate = [&db](const std::string &s) -> bool {
        if (s.empty())
        {
            return false;
        }
        try
        {
            db->execSqlSync("select $1::date", s);
            return true;
        }
        catch (...)
        {
            return false;
        }
    };
    if (!isValidDate(startDate) || !isValidDate(endDate) || startDate > endDate)
    {
        result.setResult(ApiErrorCode::ApiError_SubscriptionInvalid);
        return result;
    }

    try
    {
        auto teamRows = db->execSqlSync("select 1 from team where id = $1", teamId);
        if (teamRows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_TeamNotFound);
            return result;
        }

        const std::string planValue = plan.empty() ? "standard" : plan;
        db->execSqlSync(
            "insert into team_subscription (team_id, plan, start_date, end_date, status, updated_by) "
            "values ($1, $2, $3::date, $4::date, 1, $5)",
            teamId, planValue, startDate, endDate, adminUserId);

        result.jsondata["teamId"] = teamId;
        result.jsondata["startDate"] = startDate;
        result.jsondata["endDate"] = endDate;
        result.setResult(ApiErrorCode::ApiError_Success, "付费期已更新");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "AdminSetSubscription 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult TeamService::AdminSetFeatureAuth(int adminUserId,
                                            int teamId,
                                            const std::string &featureCode,
                                            bool enabled)
{
    HttpResult result;
    if (featureCode != "cloud_project" && featureCode != "online_update")
    {
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "未知的功能代码");
        return result;
    }

    auto db = drogon::app().getDbClient();
    try
    {
        auto teamRows = db->execSqlSync("select 1 from team where id = $1", teamId);
        if (teamRows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_TeamNotFound);
            return result;
        }

        const std::string enabledStr = enabled ? "true" : "false";
        db->execSqlSync(
            "insert into team_feature_auth (team_id, feature_code, enabled, updated_by) "
            "values ($1, $2, $3::text::boolean, $4) "
            "on conflict (team_id, feature_code) "
            "do update set enabled = excluded.enabled, updated_by = excluded.updated_by, updated_at = now()",
            teamId, featureCode, enabledStr, adminUserId);

        result.jsondata["teamId"] = teamId;
        result.jsondata["featureCode"] = featureCode;
        result.jsondata["enabled"] = enabled;
        result.setResult(ApiErrorCode::ApiError_Success, "授权已更新");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "AdminSetFeatureAuth 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

// ---------------------------------------------------------------- 超级管理员

HttpResult TeamService::AdminGetTeam(int teamId)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        auto teamRows = db->execSqlSync(
            "select t.id, t.name, t.status, t.owner_user_id, u.name as owner_name, "
            "       to_char(t.created_at, 'YYYY-MM-DD') as created_at "
            "from team t join \"user\" u on u.id = t.owner_user_id "
            "where t.id = $1",
            teamId);
        if (teamRows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_TeamNotFound);
            return result;
        }

        const auto &row = teamRows[0];
        result.jsondata["team"]["id"] = row["id"].as<int>();
        result.jsondata["team"]["name"] = row["name"].as<std::string>();
        result.jsondata["team"]["status"] = row["status"].as<int>();
        result.jsondata["team"]["ownerUserId"] = row["owner_user_id"].as<int>();
        result.jsondata["team"]["ownerName"] = row["owner_name"].as<std::string>();
        result.jsondata["team"]["createdAt"] = row["created_at"].as<std::string>();

        result.jsondata["subscription"] = LoadSubscription(db, teamId);
        result.jsondata["featureAuth"] = LoadFeatureAuth(db, teamId);
        result.jsondata["members"] = LoadTeamMembers(db, teamId);
        result.jsondata["memberCount"] = static_cast<Json::UInt>(result.jsondata["members"].size());

        result.setResult(ApiErrorCode::ApiError_Success, "success");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "AdminGetTeam 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult TeamService::AdminUpdateTeam(int adminUserId,
                                        int teamId,
                                        const std::string &name,
                                        int status)
{
    (void)adminUserId;
    HttpResult result;
    if (status != 0 && status != 1 && status != 2)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "团队状态只能是 1(正常) 或 2(停用)");
        return result;
    }
    if (name.empty() && status == 0)
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "name / status 至少提供一个");
        return result;
    }

    auto db = drogon::app().getDbClient();
    try
    {
        auto teamRows = db->execSqlSync("select 1 from team where id = $1", teamId);
        if (teamRows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_TeamNotFound);
            return result;
        }

        if (!name.empty())
        {
            auto dupRows = db->execSqlSync("select 1 from team where name = $1 and id <> $2",
                                           name, teamId);
            if (!dupRows.empty())
            {
                result.setResult(ApiErrorCode::ApiError_CodeAlreadyExists, "团队名称已存在");
                return result;
            }
            db->execSqlSync("update team set name = $2 where id = $1", teamId, name);
        }
        if (status != 0)
        {
            // status 列是 smallint，驱动按 int4 绑定会报二进制格式错误，显式转 int
            db->execSqlSync("update team set status = $2::int where id = $1", teamId, status);
        }

        result.jsondata["teamId"] = teamId;
        result.jsondata["name"] = name;
        result.jsondata["status"] = status;
        result.setResult(ApiErrorCode::ApiError_Success, "团队已更新");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "AdminUpdateTeam 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult TeamService::AdminUpdateMember(int adminUserId,
                                          int teamId,
                                          int memberUserId,
                                          const std::string &roleCode,
                                          int status)
{
    (void)adminUserId;
    HttpResult result;
    if (status != 0 && status != 1 && status != 2)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "成员状态只能是 1(正常) 或 2(停用)");
        return result;
    }
    if (roleCode.empty() && status == 0)
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "roleCode / status 至少提供一个");
        return result;
    }

    auto db = drogon::app().getDbClient();
    try
    {
        auto teamRows = db->execSqlSync("select owner_user_id from team where id = $1", teamId);
        if (teamRows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_TeamNotFound);
            return result;
        }
        const int ownerUserId = teamRows[0]["owner_user_id"].as<int>();

        auto memberRows = db->execSqlSync(
            "select 1 from team_member where team_id = $1 and user_id = $2",
            teamId, memberUserId);
        if (memberRows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_NotTeamMember, "该账号不在这个团队");
            return result;
        }

        // 创建者是团队管理员的兜底：停用或降级会让团队失去唯一可管理的人
        if (memberUserId == ownerUserId &&
            (status == 2 || (!roleCode.empty() && roleCode != "team_admin")))
        {
            result.setResult(ApiErrorCode::ApiError_TeamOwnerProtected);
            return result;
        }

        if (!roleCode.empty())
        {
            // 只允许团队角色（scope = 2），避免把系统角色挂到团队成员位
            auto roleRows = db->execSqlSync(
                "select id from role where code = $1 and scope = 2", roleCode);
            if (roleRows.empty())
            {
                result.setResult(ApiErrorCode::ApiError_InvalidRoleCode);
                return result;
            }
            db->execSqlSync(
                "update team_member set role_id = $3 where team_id = $1 and user_id = $2",
                teamId, memberUserId, roleRows[0]["id"].as<int>());
            // 厂商后台仍是单角色入口：同步关联表，避免多角色表与主角色打架
            db->execSqlSync(
                "delete from team_member_role where team_member_id in "
                "(select id from team_member where team_id = $1 and user_id = $2)",
                teamId, memberUserId);
            db->execSqlSync(
                "insert into team_member_role (team_member_id, role_id) "
                "select id, $3 from team_member where team_id = $1 and user_id = $2 "
                "on conflict do nothing",
                teamId, memberUserId, roleRows[0]["id"].as<int>());
        }
        if (status != 0)
        {
            db->execSqlSync(
                "update team_member set status = $3::int where team_id = $1 and user_id = $2",
                teamId, memberUserId, status);
        }

        Json::Value members = LoadTeamMembers(db, teamId);
        for (const auto &m : members)
        {
            if (m["userId"].asInt() == memberUserId)
            {
                result.jsondata["member"] = m;
                break;
            }
        }
        result.setResult(ApiErrorCode::ApiError_Success, "成员已更新");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "AdminUpdateMember 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult TeamService::AdminListUsers(const std::string &keyword, int limit)
{
    HttpResult result;
    if (limit <= 0)
    {
        limit = 50;
    }
    if (limit > 200)
    {
        limit = 200;
    }

    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync(
            "select u.id, u.name, u.nick_name, u.status, "
            "       tm.team_id, t.name as team_name, r.code as role_code, "
            "       ((coalesce(u.privilege, 0) & $3::int) <> 0 "
            "        or exists(select 1 from user_system_role usr "
            "                   join role sr on sr.id = usr.role_id "
            "                   where usr.user_id = u.id and sr.code = 'super_admin')) as is_super_admin "
            "from \"user\" u "
            "left join team_member tm on tm.user_id = u.id "
            "left join team t on t.id = tm.team_id "
            "left join role r on r.id = tm.role_id "
            "where $1::text = '' or u.name ilike '%' || $1 || '%' "
            "   or coalesce(u.nick_name, '') ilike '%' || $1 || '%' "
            "order by u.id limit $2::int",
            keyword, limit, static_cast<int>(UserPrivileges::Admin));

        Json::Value users(Json::arrayValue);
        for (const auto &r : rows)
        {
            Json::Value u;
            u["userId"] = r["id"].as<int>();
            u["userName"] = r["name"].as<std::string>();
            u["nickName"] = r["nick_name"].as<std::string>();
            u["status"] = r["status"].as<int>();
            u["teamId"] = r["team_id"].isNull() ? 0 : r["team_id"].as<int>();
            u["teamName"] = r["team_name"].isNull() ? "" : r["team_name"].as<std::string>();
            u["roleCode"] = r["role_code"].isNull() ? "" : r["role_code"].as<std::string>();
            u["isSuperAdmin"] = r["is_super_admin"].as<bool>();
            users.append(u);
        }
        result.jsondata["users"] = users;
        result.setResult(ApiErrorCode::ApiError_Success, "success");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "AdminListUsers 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult TeamService::AdminSetSystemRole(int operatorUserId,
                                           int targetUserId,
                                           const std::string &roleCode,
                                           bool enabled)
{
    HttpResult result;
    if (roleCode != "super_admin")
    {
        result.setResult(ApiErrorCode::ApiError_InvalidRoleCode, "本期只支持 super_admin");
        return result;
    }

    auto db = drogon::app().getDbClient();
    try
    {
        // 授予权只认 Admin 位：拿到角色的账号不能自行扩散
        auto opRows = db->execSqlSync(
            "select (coalesce(privilege, 0) & $2::int) <> 0 as bit_admin "
            "from \"user\" where id = $1",
            operatorUserId, static_cast<int>(UserPrivileges::Admin));
        if (opRows.empty() || !opRows[0]["bit_admin"].as<bool>())
        {
            result.setResult(ApiErrorCode::ApiError_AdminPrivilegeRequired,
                             "只有厂商管理员可以授予或回收系统角色");
            return result;
        }

        auto roleRows = db->execSqlSync(
            "select id from role where code = $1 and scope = 1", roleCode);
        if (roleRows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_InvalidRoleCode);
            return result;
        }
        const int roleId = roleRows[0]["id"].as<int>();

        auto userRows = db->execSqlSync("select 1 from \"user\" where id = $1", targetUserId);
        if (userRows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_UserNotFound);
            return result;
        }

        if (enabled)
        {
            db->execSqlSync(
                "insert into user_system_role (user_id, role_id, granted_by) "
                "values ($1, $2, $3) on conflict do nothing",
                targetUserId, roleId, operatorUserId);
        }
        else
        {
            db->execSqlSync(
                "delete from user_system_role where user_id = $1 and role_id = $2",
                targetUserId, roleId);
        }

        result.jsondata["userId"] = targetUserId;
        result.jsondata["roleCode"] = roleCode;
        result.jsondata["enabled"] = enabled;
        result.setResult(ApiErrorCode::ApiError_Success, enabled ? "已授予超级管理员" : "已回收超级管理员");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "AdminSetSystemRole 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

} // namespace Services
} // namespace UEAdminAPI

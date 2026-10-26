#include "TeamService.h"

#include <drogon/drogon.h>
#include "models/User.h"

#include "services/AuthService.h"
#include "utils/ApiErrorCodes.h"
#include "utils/EnumUserPrivileges.h"

#include <algorithm>
#include <cctype>

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

} // namespace

HttpResult TeamService::GetBootstrap(int userId)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync(
            "select u.id, u.privilege, u.must_change_pwd, "
            "       coalesce(b.balance_cents, 0) as balance_cents, "
            "       t.id as team_id, t.name as team_name, r.code as role_code, "
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
            userId);

        if (rows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_UserNotFound);
            return result;
        }

        const auto &row = rows[0];
        result.jsondata["userId"] = row["id"].as<int>();
        result.jsondata["privilege"] = row["privilege"].as<int>();
        result.jsondata["mustChangePwd"] = row["must_change_pwd"].as<bool>();
        result.jsondata["balanceCents"] =
            static_cast<Json::Int64>(row["balance_cents"].as<int64_t>());

        int teamId = 0;
        if (!row["team_id"].isNull())
        {
            teamId = row["team_id"].as<int>();
            result.jsondata["team"]["id"] = teamId;
            result.jsondata["team"]["name"] = row["team_name"].as<std::string>();
            result.jsondata["team"]["roleCode"] = row["role_code"].as<std::string>();
        }

        result.jsondata["subscription"]["startDate"] =
            row["sub_start"].isNull() ? "" : row["sub_start"].as<std::string>();
        result.jsondata["subscription"]["endDate"] =
            row["sub_end"].isNull() ? "" : row["sub_end"].as<std::string>();
        result.jsondata["subscription"]["active"] =
            !row["paid"].isNull() && row["paid"].as<bool>();

        // 功能授权：账号级覆盖团队级，缺失视为关闭
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
            "select id, name, status from team where id = $1", teamId);
        if (teamRows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_TeamNotFound);
            return result;
        }

        result.jsondata["team"]["id"] = teamId;
        result.jsondata["team"]["name"] = teamRows[0]["name"].as<std::string>();
        result.jsondata["team"]["roleCode"] = roleCode;

        auto subRows = db->execSqlSync(
            "select to_char(s.start_date, 'YYYY-MM-DD') as sub_start, "
            "       to_char(s.end_date, 'YYYY-MM-DD') as sub_end, "
            "       (s.end_date >= current_date) as paid "
            "from team_subscription s "
            "where s.team_id = $1 and s.status = 1 "
            "order by s.end_date desc limit 1",
            teamId);
        if (!subRows.empty())
        {
            result.jsondata["subscription"]["startDate"] = subRows[0]["sub_start"].as<std::string>();
            result.jsondata["subscription"]["endDate"] = subRows[0]["sub_end"].as<std::string>();
            result.jsondata["subscription"]["active"] = subRows[0]["paid"].as<bool>();
        }
        else
        {
            result.jsondata["subscription"]["startDate"] = "";
            result.jsondata["subscription"]["endDate"] = "";
            result.jsondata["subscription"]["active"] = false;
        }

        auto memberRows = db->execSqlSync(
            "select u.id, u.name, u.nick_name, r.code as role_code, r.name as role_name, "
            "       tm.status, u.must_change_pwd, "
            "       to_char(tm.created_at, 'YYYY-MM-DD HH24:MI:SS') as created_at "
            "from team_member tm "
            "join \"user\" u on u.id = tm.user_id "
            "join role r on r.id = tm.role_id "
            "where tm.team_id = $1 "
            "order by tm.id",
            teamId);

        Json::Value members(Json::arrayValue);
        for (const auto &r : memberRows)
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
            members.append(m);
        }
        result.jsondata["members"] = members;

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
                                     const std::string &roleCode,
                                     const std::string &nickName)
{
    HttpResult result;
    if (userName.empty() || password.size() < 6)
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "用户名不能为空，初始密码至少 6 位");
        return result;
    }
    // 只允许分配普通团队角色，避免团队管理员越权创建另一个管理员
    if (roleCode != "engineer" && roleCode != "pm")
    {
        result.setResult(ApiErrorCode::ApiError_InvalidRoleCode);
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

        auto roleRows = db->execSqlSync(
            "select id from role where code = $1 and scope = 2", roleCode);
        if (roleRows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_InvalidRoleCode);
            return result;
        }
        const int roleId = roleRows[0]["id"].as<int>();

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
            db->execSqlSync(
                "insert into team_member (team_id, user_id, role_id, status) values ($1, $2, $3, 1)",
                teamId, newUserId, roleId);
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

} // namespace Services
} // namespace UEAdminAPI

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

constexpr int kDefaultTrialDays = 30;

int SubscriptionTrialDays()
{
    const auto &cfg = drogon::app().getCustomConfig();
    if (cfg.isMember("Subscription") && cfg["Subscription"].isObject())
    {
        const auto &node = cfg["Subscription"]["TrialDays"];
        if (node.isInt() && node.asInt() > 0)
        {
            return std::min(node.asInt(), 3650);
        }
    }
    return kDefaultTrialDays;
}

std::string SubscriptionExpiredMessage(bool trial)
{
    const char *key = trial ? "TrialExpiredMessage" : "ExpiredMessage";
    const char *fallback = trial
        ? "试用期已结束，请联系销售开通：0731-88808590 / 19918074079（微信同号）"
        : "团队订阅已到期，请联系销售续费：0731-88808590 / 19918074079（微信同号）";
    const auto &cfg = drogon::app().getCustomConfig();
    if (cfg.isMember("Subscription") && cfg["Subscription"].isObject())
    {
        const auto &node = cfg["Subscription"][key];
        if (node.isString() && !node.asString().empty())
        {
            return node.asString();
        }
    }
    return fallback;
}

// 套餐席位上限：0 表示不限制（存量/厂商团队）
int PlanSeatLimit(const std::string &plan, bool active)
{
    if (!active)
    {
        return 1; // Free
    }
    if (plan == "max")
    {
        return 30;
    }
    if (plan == "pro" || plan == "standard" || plan == "legacy")
    {
        return 10;
    }
    if (plan == "plus" || plan == "trial")
    {
        return 4;
    }
    return 1;
}

int SeatLimitForTeam(const std::shared_ptr<drogon::orm::DbClient> &db, int teamId, bool isVendor)
{
    if (isVendor)
    {
        return 0;
    }
    // 套餐只认 ueaicad；PDMS-only / 到期 AI + 有效 PDMS 不限制团队账号数。
    auto rows = db->execSqlSync(
        "select "
        "  (select plan from team_subscription where team_id = $1 and product = 'ueaicad' and status = 1 "
        "   order by end_date desc limit 1) as plan, "
        "  (select (end_date >= (now() at time zone 'Asia/Shanghai')::date) "
        "   from team_subscription where team_id = $1 and product = 'ueaicad' and status = 1 "
        "   order by end_date desc limit 1) as active, "
        "  exists(select 1 from team_subscription p where p.team_id = $1 and p.product = 'pdms' "
        "         and p.status = 1) as has_pdms, "
        "  exists(select 1 from team_subscription p where p.team_id = $1 and p.product = 'pdms' "
        "         and p.status = 1 and p.end_date >= (now() at time zone 'Asia/Shanghai')::date) as pdms_active, "
        "  exists(select 1 from team_subscription x where x.team_id = $1 and x.status = 1) as has_any",
        teamId);
    if (rows.empty())
    {
        return 0;
    }
    const bool hasAny = rows[0]["has_any"].as<bool>();
    if (!hasAny)
    {
        return 0; // 没有订阅记录的存量团队不限制
    }
    const bool pdmsActive = rows[0]["pdms_active"].as<bool>();
    if (rows[0]["plan"].isNull())
    {
        return pdmsActive ? 0 : 1; // 只有 PDMS：有效则不限，到期按 Free
    }
    const std::string plan = rows[0]["plan"].as<std::string>();
    const bool active = !rows[0]["active"].isNull() && rows[0]["active"].as<bool>();
    if (!active)
    {
        return pdmsActive ? 0 : 1;
    }
    return PlanSeatLimit(plan, true);
}

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

// 只有真正持有 Admin 位的账号才能授予/回收 Admin 位或处置 Admin 位账号。
// 单纯通过 user_system_role 拿到 super_admin 的账号不具备扩散能力。
bool HasAdminBit(const std::shared_ptr<drogon::orm::DbClient> &db, int userId)
{
    auto rows = db->execSqlSync(
        "select (coalesce(privilege, 0) & $2::int) <> 0 as bit_admin "
        "from \"user\" where id = $1",
        userId, static_cast<int>(UserPrivileges::Admin));
    return !rows.empty() && rows[0]["bit_admin"].as<bool>();
}

bool ResolveSuperAdminRoleId(const std::shared_ptr<drogon::orm::DbClient> &db, int &roleId)
{
    roleId = 0;
    auto rows = db->execSqlSync(
        "select id from role where code = 'super_admin' and scope = 1");
    if (rows.empty())
    {
        return false;
    }
    roleId = rows[0]["id"].as<int>();
    return true;
}

// 授予/回收超级管理员：同时改 privilege 的 Admin 位与 user_system_role，
// 与 IsSuperAdmin / /oauth2/introspect 的口径保持一致。
bool SetUserSuperAdmin(const std::shared_ptr<drogon::orm::DbClient> &db,
                       int userId,
                       bool grant,
                       std::string &err)
{
    int roleId = 0;
    if (!ResolveSuperAdminRoleId(db, roleId))
    {
        err = "缺少 super_admin 系统角色，请先执行迁移";
        return false;
    }

    if (grant)
    {
        db->execSqlSync(
            "update \"user\" set privilege = (coalesce(privilege, 0) | $2::int) where id = $1",
            userId, static_cast<int>(UserPrivileges::Admin));
        db->execSqlSync(
            "insert into user_system_role (user_id, role_id) values ($1, $2) "
            "on conflict do nothing",
            userId, roleId);
    }
    else
    {
        db->execSqlSync(
            "update \"user\" set privilege = (coalesce(privilege, 0) & ~($2::int)) where id = $1",
            userId, static_cast<int>(UserPrivileges::Admin));
        db->execSqlSync(
            "delete from user_system_role where user_id = $1 and role_id = $2",
            userId, roleId);
    }
    return true;
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

// 该用户是否是该团队的团队管理员：主角色与多角色表任一命中都算
bool IsTeamAdminOf(const std::shared_ptr<drogon::orm::DbClient> &db, int userId, int teamId)
{
    auto rows = db->execSqlSync(
        "select 1 from team_member tm join role r on r.id = tm.role_id "
        "where tm.user_id = $1 and tm.team_id = $2 and tm.status = 1 and r.code = 'team_admin' "
        "union all "
        "select 1 from team_member tm "
        "join team_member_role tmr on tmr.team_member_id = tm.id "
        "join role r on r.id = tmr.role_id "
        "where tm.user_id = $1 and tm.team_id = $2 and tm.status = 1 and r.code = 'team_admin' "
        "limit 1",
        userId, teamId);
    return !rows.empty();
}

// 团队里有效团队管理员的人数（主角色 ∪ 多角色表去重）
int CountTeamAdmins(const std::shared_ptr<drogon::orm::DbClient> &db, int teamId)
{
    auto rows = db->execSqlSync(
        "select count(*) as cnt from ("
        "  select tm.user_id from team_member tm join role r on r.id = tm.role_id "
        "  where tm.team_id = $1 and tm.status = 1 and r.code = 'team_admin' "
        "  union "
        "  select tm.user_id from team_member tm "
        "  join team_member_role tmr on tmr.team_member_id = tm.id "
        "  join role r on r.id = tmr.role_id "
        "  where tm.team_id = $1 and tm.status = 1 and r.code = 'team_admin'"
        ") x",
        teamId);
    return rows.empty() ? 0 : static_cast<int>(rows[0]["cnt"].as<int64_t>());
}

// 厂商跨团队操作者：超级管理员，或厂商团队里的团队管理员
bool IsVendorOperator(const std::shared_ptr<drogon::orm::DbClient> &db,
                      int userId,
                      int &ownTeamId,
                      std::string &roleCode)
{
    ownTeamId = 0;
    roleCode.clear();
    auto rows = db->execSqlSync(
        "select tm.team_id, r.code as role_code, t.is_vendor "
        "from team_member tm "
        "join role r on r.id = tm.role_id "
        "join team t on t.id = tm.team_id "
        "where tm.user_id = $1 and tm.status = 1",
        userId);
    if (!rows.empty())
    {
        ownTeamId = rows[0]["team_id"].as<int>();
        roleCode = rows[0]["role_code"].as<std::string>();
    }
    // 与 AdminFilter 口径一致：不在厂商团队的超级管理员同样放行
    if (IsSuperAdmin(db, userId))
    {
        return true;
    }
    return !rows.empty() && rows[0]["is_vendor"].as<bool>() &&
           IsTeamAdminOf(db, userId, ownTeamId);
}

// 个人团队：注册时自动创建；允许改名后必须靠 is_personal 识别，不能再依赖名字
bool ResolvePersonalTeam(const std::shared_ptr<drogon::orm::DbClient> &db,
                         int userId,
                         const std::string &userName,
                         int &teamId)
{
    teamId = 0;
    auto rows = db->execSqlSync(
        "select id from team where is_personal = true and owner_user_id = $1 order by id limit 1",
        userId);
    if (!rows.empty())
    {
        teamId = rows[0]["id"].as<int>();
        return true;
    }

    const std::string teamName = "Team-" + std::to_string(userId) + "-" + userName;
    auto inserted = db->execSqlSync(
        "insert into team (name, owner_user_id, status, is_personal) values ($1, $2, 1, true) "
        "on conflict (name) do update set is_personal = true "
        "returning id",
        teamName, userId);
    if (inserted.empty())
    {
        return false;
    }
    teamId = inserted[0]["id"].as<int>();
    return true;
}

// 把成员整体移动到目标团队并替换角色。连接池下 begin/commit 不保证同连接，
// 因此失败时按原值回滚（与 CreateMember 删用户的做法一致）。
bool MoveMemberToTeam(const std::shared_ptr<drogon::orm::DbClient> &db,
                      int memberUserId,
                      int targetTeamId,
                      const std::vector<int> &roleIds,
                      std::string &err)
{
    if (roleIds.empty())
    {
        err = "至少保留一个团队角色";
        return false;
    }

    auto rows = db->execSqlSync(
        "select id, team_id, role_id, status from team_member where user_id = $1", memberUserId);
    if (rows.empty())
    {
        err = "该账号不属于任何团队";
        return false;
    }
    const int memberId = rows[0]["id"].as<int>();
    const int oldTeamId = rows[0]["team_id"].as<int>();
    const int oldRoleId = rows[0]["role_id"].as<int>();
    const int oldStatus = rows[0]["status"].as<int>();

    std::vector<int> oldRoleIds;
    auto oldRoleRows = db->execSqlSync(
        "select role_id from team_member_role where team_member_id = $1", memberId);
    for (const auto &r : oldRoleRows)
    {
        oldRoleIds.push_back(r["role_id"].as<int>());
    }

    auto restore = [&]() {
        try
        {
            db->execSqlSync(
                "update team_member set team_id = $2, role_id = $3, status = $4 where id = $1",
                memberId, oldTeamId, oldRoleId, oldStatus);
            db->execSqlSync("delete from team_member_role where team_member_id = $1", memberId);
            for (const int roleId : oldRoleIds)
            {
                db->execSqlSync(
                    "insert into team_member_role (team_member_id, role_id) values ($1, $2) "
                    "on conflict do nothing",
                    memberId, roleId);
            }
        }
        catch (...)
        {
            LOG_ERROR << "MoveMemberToTeam 回滚失败, memberId=" << memberId;
        }
    };

    try
    {
        db->execSqlSync(
            "update team_member set team_id = $2, role_id = $3, status = 1 where id = $1",
            memberId, targetTeamId, roleIds[0]);
        db->execSqlSync("delete from team_member_role where team_member_id = $1", memberId);
        for (const int roleId : roleIds)
        {
            db->execSqlSync(
                "insert into team_member_role (team_member_id, role_id) values ($1, $2) "
                "on conflict do nothing",
                memberId, roleId);
        }
        db->execSqlSync("insert into user_balance (user_id) values ($1) on conflict do nothing",
                        memberUserId);
        return true;
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        err = e.base().what();
        restore();
        return false;
    }
}

std::string TrimText(const std::string &s)
{
    const size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
    {
        return "";
    }
    const size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
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
Json::Value LoadSubscription(const std::shared_ptr<drogon::orm::DbClient> &db,
                             int teamId,
                             const std::string &product = "ueaicad")
{
    Json::Value sub;
    // subscription 视图固定为 UEAICAD；PDMS 记录不得混入 AI 套餐/到期天数。
    auto rows = db->execSqlSync(
        "select to_char(s.start_date, 'YYYY-MM-DD') as sub_start, "
        "       to_char(s.end_date, 'YYYY-MM-DD') as sub_end, "
        "       coalesce(s.is_trial, false) as is_trial, "
        "       s.plan, "
        "       (s.end_date >= (now() at time zone 'Asia/Shanghai')::date) as paid, "
        "       (s.end_date - (now() at time zone 'Asia/Shanghai')::date) as remaining_days "
        "from team_subscription s "
        "where s.team_id = $1 and s.status = 1 and s.product = $2 "
        "order by s.end_date desc limit 1",
        teamId, product);
    if (!rows.empty())
    {
        sub["startDate"] = rows[0]["sub_start"].as<std::string>();
        sub["endDate"] = rows[0]["sub_end"].as<std::string>();
        sub["active"] = rows[0]["paid"].as<bool>();
        sub["plan"] = rows[0]["plan"].isNull() ? "" : rows[0]["plan"].as<std::string>();
        sub["isTrial"] = rows[0]["is_trial"].as<bool>();
        sub["remainingDays"] = rows[0]["remaining_days"].isNull() ? 0 : rows[0]["remaining_days"].as<int>();
    }
    else
    {
        sub["startDate"] = "";
        sub["endDate"] = "";
        sub["active"] = false;
        sub["plan"] = "";
        sub["isTrial"] = false;
        sub["remainingDays"] = 0;
    }
    return sub;
}

struct ProductEntitlements
{
    Json::Value products;
    bool hasRecord = false;
};

// 每产品取最新一条记录；AutoPDMS 只认自己的产品记录，不再由 UEAICAD 推导。
// hasRecord 含已到期记录，用于区分“真存量”与“AutoPDMS-only/已到期”。
ProductEntitlements LoadProductEntitlements(const std::shared_ptr<drogon::orm::DbClient> &db,
                                           int teamId)
{
    ProductEntitlements ent;
    ent.products["pdms"]["active"] = false;
    ent.products["pdms"]["source"] = "none";
    ent.products["pdms"]["startDate"] = "";
    ent.products["pdms"]["endDate"] = "";
    ent.products["pdms"]["remainingDays"] = 0;
    ent.products["ueaicad"]["active"] = false;
    ent.products["ueaicad"]["source"] = "none";
    ent.products["ueaicad"]["plan"] = "";
    ent.products["ueaicad"]["startDate"] = "";
    ent.products["ueaicad"]["endDate"] = "";
    ent.products["ueaicad"]["isTrial"] = false;
    ent.products["ueaicad"]["remainingDays"] = 0;

    auto rows = db->execSqlSync(
        "select * from ("
        "  select product, plan, coalesce(is_trial, false) as is_trial, "
        "         to_char(start_date, 'YYYY-MM-DD') as sub_start, "
        "         to_char(end_date, 'YYYY-MM-DD') as sub_end, "
        "         (end_date >= (now() at time zone 'Asia/Shanghai')::date) as active, "
        "         (end_date - (now() at time zone 'Asia/Shanghai')::date) as remaining_days, "
        "         row_number() over (partition by product order by end_date desc) as rn "
        "  from team_subscription where team_id = $1 and status = 1) x where rn = 1",
        teamId);
    ent.hasRecord = !rows.empty();

    bool hasPdms = false;
    bool pdmsActive = false;
    std::string pdmsStart;
    std::string pdmsEnd;
    int pdmsRemaining = 0;
    bool hasUe = false;
    bool ueActive = false;
    std::string uePlan;
    std::string ueStart;
    std::string ueEnd;
    bool ueTrial = false;
    int ueRemaining = 0;

    for (const auto &r : rows)
    {
        const std::string product = r["product"].as<std::string>();
        if (product == "pdms")
        {
            hasPdms = true;
            pdmsActive = r["active"].as<bool>();
            pdmsStart = r["sub_start"].isNull() ? "" : r["sub_start"].as<std::string>();
            pdmsEnd = r["sub_end"].isNull() ? "" : r["sub_end"].as<std::string>();
            pdmsRemaining = r["remaining_days"].isNull() ? 0 : r["remaining_days"].as<int>();
        }
        else if (product == "ueaicad")
        {
            hasUe = true;
            ueActive = r["active"].as<bool>();
            uePlan = r["plan"].isNull() ? "" : r["plan"].as<std::string>();
            ueStart = r["sub_start"].isNull() ? "" : r["sub_start"].as<std::string>();
            ueEnd = r["sub_end"].isNull() ? "" : r["sub_end"].as<std::string>();
            ueTrial = r["is_trial"].as<bool>();
            ueRemaining = r["remaining_days"].isNull() ? 0 : r["remaining_days"].as<int>();
        }
    }

    if (hasUe)
    {
        ent.products["ueaicad"]["active"] = ueActive;
        ent.products["ueaicad"]["source"] = "subscription";
        ent.products["ueaicad"]["plan"] = uePlan;
        ent.products["ueaicad"]["startDate"] = ueStart;
        ent.products["ueaicad"]["endDate"] = ueEnd;
        ent.products["ueaicad"]["isTrial"] = ueTrial;
        ent.products["ueaicad"]["remainingDays"] = ueRemaining;
    }
    else if (!ent.hasRecord)
    {
        ent.products["ueaicad"]["active"] = true;
        ent.products["ueaicad"]["source"] = "legacy";
    }

    if (hasPdms)
    {
        ent.products["pdms"]["active"] = pdmsActive;
        ent.products["pdms"]["source"] = "subscription";
        ent.products["pdms"]["startDate"] = pdmsStart;
        ent.products["pdms"]["endDate"] = pdmsEnd;
        ent.products["pdms"]["remainingDays"] = pdmsRemaining;
    }
    else if (!ent.hasRecord)
    {
        ent.products["pdms"]["active"] = true;
        ent.products["pdms"]["source"] = "legacy";
    }
    return ent;
}

void ApplyVendorProducts(Json::Value &products)
{
    products["pdms"]["active"] = true;
    products["pdms"]["source"] = "vendor";
    products["ueaicad"]["active"] = true;
    products["ueaicad"]["source"] = "vendor";
    products["ueaicad"]["plan"] = "max";
    products["ueaicad"]["isTrial"] = false;
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

Json::Value LoadTeamMembers(const std::shared_ptr<drogon::orm::DbClient> &db,
                            int teamId,
                            bool isVendor)
{
    auto rows = db->execSqlSync(
        "select tm.id as member_id, u.id, u.name, u.nick_name, r.code as role_code, r.name as role_name, "
        "       tm.status, u.must_change_pwd, "
        "       to_char(tm.created_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD HH24:MI:SS') as created_at "
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

    // 厂商团队把系统角色 super_admin 也显示出来（privilege Admin 位或 user_system_role 都算）
    std::map<int, bool> superAdmins;
    if (isVendor)
    {
        auto adminRows = db->execSqlSync(
            "select u.id, "
            "       ((coalesce(u.privilege, 0) & $2::int) <> 0 "
            "        or exists(select 1 from user_system_role usr "
            "                   join role sr on sr.id = usr.role_id "
            "                   where usr.user_id = u.id and sr.code = 'super_admin')) as is_super_admin "
            "from \"user\" u "
            "where u.id in (select tm.user_id from team_member tm where tm.team_id = $1)",
            teamId, static_cast<int>(UserPrivileges::Admin));
        for (const auto &row : adminRows)
        {
            superAdmins[row["id"].as<int>()] = row["is_super_admin"].as<bool>();
        }
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
        const int userId = r["id"].as<int>();

        Json::Value roles(Json::arrayValue);
        auto superIt = superAdmins.find(userId);
        if (superIt != superAdmins.end() && superIt->second)
        {
            Json::Value role;
            role["code"] = "super_admin";
            role["name"] = "超级管理员";
            roles.append(role);
        }

        auto it = rolesByMember.find(memberId);
        if (it != rolesByMember.end())
        {
            for (const auto &role : it->second)
            {
                roles.append(role);
            }
        }
        else
        {
            // 迁移未执行/未回填时退回主角色，界面不至于显示空白
            Json::Value role;
            role["code"] = r["role_code"].as<std::string>();
            role["name"] = r["role_name"].as<std::string>();
            roles.append(role);
        }
        m["roles"] = roles;
        members.append(m);
    }
    return members;
}

} // namespace

// 厂商控制台：只有厂商团队里的团队管理员可进入（超级管理员若不在厂商团队同样不放行）
bool TeamService::IsVendorTeamAdmin(int userId)
{
    try
    {
        auto db = drogon::app().getDbClient();
        auto rows = db->execSqlSync(
            "select tm.team_id from team_member tm "
            "join team t on t.id = tm.team_id "
            "where tm.user_id = $1 and tm.status = 1 and coalesce(t.is_vendor, false) "
            "limit 1",
            userId);
        if (rows.empty())
        {
            return false;
        }
        return IsTeamAdminOf(db, userId, rows[0]["team_id"].as<int>());
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "TeamService::IsVendorTeamAdmin 查询失败: " << e.base().what();
        return false;
    }
}

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
            "       coalesce(t.is_vendor, false) as team_is_vendor, "
            "       to_char(s.start_date, 'YYYY-MM-DD') as sub_start, "
            "       to_char(s.end_date, 'YYYY-MM-DD') as sub_end, "
            "       coalesce(s.is_trial, false) as sub_is_trial, "
            "       s.plan as sub_plan, "
            "       (s.end_date is not null and s.end_date >= (now() at time zone 'Asia/Shanghai')::date) as paid, "
            "       (s.end_date - (now() at time zone 'Asia/Shanghai')::date) as sub_remaining_days "
            "from \"user\" u "
            "left join user_balance b on b.user_id = u.id "
            "left join team_member tm on tm.user_id = u.id and tm.status = 1 "
            "left join team t on t.id = tm.team_id "
            "left join role r on r.id = tm.role_id "
            "left join lateral (select start_date, end_date, plan, is_trial from team_subscription ts "
            "                   where ts.team_id = t.id and ts.status = 1 and ts.product = 'ueaicad' "
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
        const bool isSuperAdmin = row["is_super_admin"].as<bool>();
        const bool isVendorTeam = !row["team_is_vendor"].isNull() && row["team_is_vendor"].as<bool>();
        const bool vendorMax = isVendorTeam && isSuperAdmin;
        result.jsondata["isSuperAdmin"] = isSuperAdmin;
        result.jsondata["isVendor"] = isVendorTeam;
        result.jsondata["canVendorConsole"] = isSuperAdmin;
        if (vendorMax) result.jsondata["vendorMax"] = true;
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
            result.jsondata["team"]["isVendor"] = isVendorTeam;
            result.jsondata["canVendorConsole"] =
                isSuperAdmin || (isVendorTeam && IsTeamAdminOf(db, userId, teamId));
        }

        result.jsondata["subscription"]["startDate"] =
            row["sub_start"].isNull() ? "" : row["sub_start"].as<std::string>();
        result.jsondata["subscription"]["endDate"] =
            row["sub_end"].isNull() ? "" : row["sub_end"].as<std::string>();
        result.jsondata["subscription"]["active"] =
            !row["paid"].isNull() && row["paid"].as<bool>();
        result.jsondata["subscription"]["plan"] =
            row["sub_plan"].isNull() ? "" : row["sub_plan"].as<std::string>();
        result.jsondata["subscription"]["isTrial"] = row["sub_is_trial"].as<bool>();
        result.jsondata["subscription"]["remainingDays"] =
            row["sub_remaining_days"].isNull() ? 0 : row["sub_remaining_days"].as<int>();
        if (vendorMax)
        {
            result.jsondata["subscription"]["startDate"] = "";
            result.jsondata["subscription"]["endDate"] = "";
            result.jsondata["subscription"]["active"] = true;
            result.jsondata["subscription"]["plan"] = "max";
            result.jsondata["subscription"]["isTrial"] = false;
            result.jsondata["subscription"]["remainingDays"] = 0;
        }

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
        if (vendorMax)
        {
            cloudAuth = true;
            updateAuth = true;
        }
        result.jsondata["featureAuth"]["cloud_project"] = cloudAuth;
        result.jsondata["featureAuth"]["online_update"] = updateAuth;

        auto ent = LoadProductEntitlements(db, teamId);
        if (vendorMax)
        {
            ApplyVendorProducts(ent.products);
        }
        result.jsondata["hasSubscriptionRecord"] = ent.hasRecord;
        result.jsondata["products"] = ent.products;

        int addonCredits = 0;
        int addonStorageGb = 0;
        if (teamId > 0)
        {
            try
            {
                auto addonRows = db->execSqlSync(
                    "select kind, coalesce(sum(amount), 0) as amount from team_addon "
                    "where team_id = $1 and status = 1 and end_at > now() group by kind",
                    teamId);
                for (const auto &ar : addonRows)
                {
                    const std::string kind = ar["kind"].as<std::string>();
                    if (kind == "credits")
                    {
                        addonCredits = ar["amount"].as<int>();
                    }
                    else if (kind == "storage")
                    {
                        addonStorageGb = ar["amount"].as<int>();
                    }
                }
            }
            catch (const drogon::orm::DrogonDbException &e)
            {
                LOG_WARN << "读取团队加购失败: " << e.base().what();
            }
        }
        result.jsondata["addons"]["credits"] = addonCredits;
        result.jsondata["addons"]["storageGb"] = addonStorageGb;

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
            "select id, name, status, owner_user_id, is_vendor from team where id = $1", teamId);
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
        const bool isVendor = teamRows[0]["is_vendor"].as<bool>();
        const bool superAdmin = IsSuperAdmin(db, userId);
        const bool vendorMax = isVendor && superAdmin;
        result.jsondata["isVendor"] = isVendor;
        if (vendorMax) result.jsondata["vendorMax"] = true;
        // 客户端据此决定是否把「团队管理员」/「超级管理员」作为可勾选角色
        result.jsondata["canGrantTeamAdmin"] = CanGrantTeamAdmin(db, userId, teamId);
        result.jsondata["canGrantSuperAdmin"] = isVendor && superAdmin;
        result.jsondata["team"]["isVendor"] = isVendor;

        Json::Value companySubscription = LoadSubscription(db, teamId);
        if (vendorMax)
        {
            companySubscription["startDate"] = "";
            companySubscription["endDate"] = "";
            companySubscription["active"] = true;
            companySubscription["plan"] = "max";
            companySubscription["isTrial"] = false;
            companySubscription["remainingDays"] = 0;
        }
        result.jsondata["subscription"] = companySubscription;

        auto ent = LoadProductEntitlements(db, teamId);
        if (vendorMax)
        {
            ApplyVendorProducts(ent.products);
        }
        result.jsondata["hasSubscriptionRecord"] = ent.hasRecord;
        result.jsondata["products"] = ent.products;

        result.jsondata["members"] = LoadTeamMembers(db, teamId, isVendor);

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
                                     const std::vector<std::string> &systemRoleCodes,
                                     const std::string &nickName)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    int teamId = 0;
    try
    {
        std::string operatorRole;
        if (!GetMembership(db, operatorUserId, teamId, operatorRole))
        {
            result.setResult(ApiErrorCode::ApiError_NotTeamMember);
            return result;
        }
        if (!IsTeamAdminOf(db, operatorUserId, teamId))
        {
            result.setResult(ApiErrorCode::ApiError_NotTeamAdmin);
            return result;
        }
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "CreateMember 校验操作者失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
        return result;
    }
    return CreateMemberInTeam(operatorUserId, teamId, userName, password, roleCodes,
                              systemRoleCodes, nickName);
}

HttpResult TeamService::CreateMemberInTeam(int operatorUserId,
                                           int teamId,
                                           const std::string &userName,
                                           const std::string &password,
                                           const std::vector<std::string> &roleCodes,
                                           const std::vector<std::string> &systemRoleCodes,
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
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "至少选择一个团队角色");
        return result;
    }

    auto db = drogon::app().getDbClient();
    try
    {
        auto teamRows = db->execSqlSync("select status, is_vendor from team where id = $1", teamId);
        if (teamRows.empty() || teamRows[0]["status"].as<int>() != 1)
        {
            result.setResult(ApiErrorCode::ApiError_InvalidOperation, "团队已停用，不能新建成员");
            return result;
        }
        const bool isVendor = teamRows[0]["is_vendor"].as<bool>();

        const int seatLimit = SeatLimitForTeam(db, teamId, isVendor);
        if (seatLimit > 0)
        {
            auto countRows = db->execSqlSync(
                "select count(*)::int as used from team_member "
                "where team_id = $1 and status = 1",
                teamId);
            const int used = countRows.empty() ? 0 : countRows[0]["used"].as<int>();
            if (used >= seatLimit)
            {
                result.setResult(ApiErrorCode::ApiError_InvalidOperation,
                                 "团队席位已满（" + std::to_string(used) + "/" +
                                     std::to_string(seatLimit) +
                                     "），请升级套餐或增购席位");
                return result;
            }
        }

        const bool grantSuper = !systemRoleCodes.empty();
        if (grantSuper)
        {
            for (const auto &code : systemRoleCodes)
            {
                if (code != "super_admin")
                {
                    result.setResult(ApiErrorCode::ApiError_InvalidRoleCode, "不支持的系统角色");
                    return result;
                }
            }
            if (!isVendor)
            {
                result.setResult(ApiErrorCode::ApiError_AdminPrivilegeRequired,
                                 "只有厂商团队可以配置超级管理员");
                return result;
            }
            if (!HasAdminBit(db, operatorUserId))
            {
                result.setResult(ApiErrorCode::ApiError_AdminPrivilegeRequired,
                                 "只有持有 Admin 位的账号可以授予超级管理员");
                return result;
            }
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

        // 先授予系统角色（失败时还没有 team_member 关联，删用户更容易回滚）
        if (grantSuper)
        {
            std::string saErr;
            if (!SetUserSuperAdmin(db, newUserId, true, saErr))
            {
                LOG_ERROR << "CreateMember 授予超级管理员失败: " << saErr;
                try
                {
                    mapperUser.deleteOne(newUser);
                }
                catch (...)
                {
                }
                result.setResult(ApiErrorCode::ApiError_InternalError, saErr);
                return result;
            }
        }

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
                                          const std::vector<std::string> &roleCodes,
                                          const std::vector<std::string> &systemRoleCodes,
                                          bool updateSystemRoles)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    int teamId = 0;
    try
    {
        std::string operatorRole;
        if (!GetMembership(db, operatorUserId, teamId, operatorRole))
        {
            result.setResult(ApiErrorCode::ApiError_NotTeamMember);
            return result;
        }
        if (!IsTeamAdminOf(db, operatorUserId, teamId))
        {
            result.setResult(ApiErrorCode::ApiError_NotTeamAdmin);
            return result;
        }
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "UpdateMemberRoles 校验操作者失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
        return result;
    }
    return UpdateMemberRolesInTeam(operatorUserId, teamId, memberUserId, roleCodes,
                                   systemRoleCodes, updateSystemRoles);
}

HttpResult TeamService::UpdateMemberRolesInTeam(int operatorUserId,
                                                int teamId,
                                                int memberUserId,
                                                const std::vector<std::string> &roleCodes,
                                                const std::vector<std::string> &systemRoleCodes,
                                                bool updateSystemRoles)
{
    HttpResult result;
    if (roleCodes.empty())
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "至少保留一个团队角色");
        return result;
    }

    auto db = drogon::app().getDbClient();
    try
    {
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

        // 撤销团队管理员同样受保护，且不能把团队清空到没有管理员
        if (!grantAdmin && IsTeamAdminOf(db, memberUserId, teamId))
        {
            if (!CanGrantTeamAdmin(db, operatorUserId, teamId))
            {
                result.setResult(ApiErrorCode::ApiError_NotTeamAdmin,
                                 "只有厂商超级管理员或团队所有者可以撤销团队管理员");
                return result;
            }
            if (CountTeamAdmins(db, teamId) <= 1)
            {
                result.setResult(ApiErrorCode::ApiError_NotTeamAdmin, "团队至少保留一个团队管理员");
                return result;
            }
        }

        auto teamRows = db->execSqlSync(
            "select owner_user_id, is_vendor from team where id = $1", teamId);
        const bool isOwner = !teamRows.empty() && !teamRows[0]["owner_user_id"].isNull() &&
                             teamRows[0]["owner_user_id"].as<int>() == memberUserId;
        if (isOwner && !grantAdmin)
        {
            result.setResult(ApiErrorCode::ApiError_TeamOwnerProtected);
            return result;
        }

        // systemRoleCodes 缺省表示老客户端：不改系统角色；显式空数组在厂商团队表示回收超管，
        // 在非厂商团队则是无操作（新客户端对所有团队都会带这个字段）。
        bool grantSuper = false;
        bool changeSystemRole = false;
        if (updateSystemRoles)
        {
            for (const auto &code : systemRoleCodes)
            {
                if (code != "super_admin")
                {
                    result.setResult(ApiErrorCode::ApiError_InvalidRoleCode, "不支持的系统角色");
                    return result;
                }
            }
            grantSuper = std::find(systemRoleCodes.begin(), systemRoleCodes.end(), "super_admin") !=
                         systemRoleCodes.end();
            const bool isVendor = !teamRows.empty() && teamRows[0]["is_vendor"].as<bool>();
            if (grantSuper && !isVendor)
            {
                result.setResult(ApiErrorCode::ApiError_AdminPrivilegeRequired,
                                 "只有厂商团队可以配置超级管理员");
                return result;
            }
            if (isVendor)
            {
                const bool targetIsSuper = IsSuperAdmin(db, memberUserId);
                changeSystemRole = (grantSuper != targetIsSuper);
                if (changeSystemRole && !HasAdminBit(db, operatorUserId))
                {
                    result.setResult(ApiErrorCode::ApiError_AdminPrivilegeRequired,
                                     "只有持有 Admin 位的账号可以配置超级管理员");
                    return result;
                }
            }
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

        if (changeSystemRole)
        {
            std::string saErr;
            if (!SetUserSuperAdmin(db, memberUserId, grantSuper, saErr))
            {
                result.setResult(ApiErrorCode::ApiError_InternalError, saErr);
                return result;
            }
        }

        Json::Value roles(Json::arrayValue);
        for (const auto &code : roleCodes)
        {
            roles.append(code);
        }
        if (updateSystemRoles)
        {
            for (const auto &code : systemRoleCodes)
            {
                roles.append(code);
            }
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
            "insert into team (name, owner_user_id, status, is_personal) values ($1, $2, 1, true) "
            "on conflict (name) do update set owner_user_id = excluded.owner_user_id, "
            "is_personal = true returning id",
            teamName, userId);
        if (teamRows.empty())
        {
            return false;
        }
        const int teamId = teamRows[0]["id"].as<int>();

        auto insertedMemberRows = db->execSqlSync(
            "insert into team_member (team_id, user_id, role_id, status) values ($1, $2, $3, 1) "
            "on conflict (user_id) do nothing returning id",
            teamId, userId, roleId);
        if (!insertedMemberRows.empty())
        {
            // 多角色表是权威来源，这里也写一份，避免 LoadTeamMembers / 改角色时兜底分支不一致
            db->execSqlSync(
                "insert into team_member_role (team_member_id, role_id) values ($1, $2) "
                "on conflict do nothing",
                insertedMemberRows[0]["id"].as<int>(), roleId);
        }
        db->execSqlSync("insert into user_balance (user_id) values ($1) on conflict do nothing",
                        userId);

        // 新注册用户同时赠送 UEAICAD 与 AutoPDMS 试用，起止日一致。
        // end_date 当天仍可用，所以按“含注册当天共 N 天”倒推。
        // 已有订阅记录的团队不覆盖（重复开通 / 后台已提前开通）。
        const int trialDays = SubscriptionTrialDays();
        db->execSqlSync(
            "insert into team_subscription (team_id, product, plan, start_date, end_date, status, is_trial) "
            "select $1, p.product, 'trial', (now() at time zone 'Asia/Shanghai')::date, "
            "       (now() at time zone 'Asia/Shanghai')::date + $2, 1, true "
            "from (values ('ueaicad'), ('pdms')) as p(product) "
            "where not exists (select 1 from team_subscription where team_id = $1)",
            teamId, trialDays - 1);

        // 试用期内与付费用户一致：开通云工程与在线更新。
        // do nothing 避免覆盖厂商后台已做的单独授权。
        db->execSqlSync(
            "insert into team_feature_auth (team_id, feature_code, enabled) "
            "values ($1, 'cloud_project', true), ($1, 'online_update', true) "
            "on conflict (team_id, feature_code) do nothing",
            teamId);
        return true;
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "ProvisionPersonalTeam 失败: " << e.base().what();
        return false;
    }
}

TeamService::SubscriptionAccess TeamService::CheckSubscriptionAccess(int userId)
{
    SubscriptionAccess access;
    auto db = drogon::app().getDbClient();
    try
    {
        auto rows = db->execSqlSync(
            "select coalesce(t.is_vendor, false) as is_vendor, "
            "       ((coalesce(u.privilege, 0) & $2::int) <> 0) as is_admin, "
            "       exists(select 1 from user_system_role usr "
            "               join role sr on sr.id = usr.role_id "
            "               where usr.user_id = u.id and sr.code = 'super_admin') as is_super_admin, "
            "       exists(select 1 from team_subscription ts_any "
            "               where ts_any.team_id = t.id and ts_any.status = 1) as has_any_subscription, "
            "       s.plan, coalesce(s.is_trial, false) as is_trial, "
            "       to_char(s.end_date, 'YYYY-MM-DD') as sub_end, "
            "       (s.end_date >= (now() at time zone 'Asia/Shanghai')::date) as sub_active, "
            "       (s.end_date - (now() at time zone 'Asia/Shanghai')::date) as remaining_days "
            "from \"user\" u "
            "left join team_member tm on tm.user_id = u.id and tm.status = 1 "
            "left join team t on t.id = tm.team_id "
            "left join lateral (select plan, end_date, is_trial from team_subscription ts "
            "                   where ts.team_id = t.id and ts.status = 1 and ts.product = 'ueaicad' "
            "                   order by ts.end_date desc limit 1) s on true "
            "where u.id = $1",
            userId, static_cast<int>(UserPrivileges::Admin));
        if (rows.empty())
        {
            access.allowed = false;
            access.denyMessage = "用户不存在";
            return access;
        }

        const auto &r = rows[0];
        // 厂商团队与超管账号不参与试用/订阅限制
        if (r["is_vendor"].as<bool>() || r["is_admin"].as<bool>() || r["is_super_admin"].as<bool>())
        {
            access.isVendorOrAdmin = true;
            return access;
        }

        access.hasSubscription = !r["sub_end"].isNull();
        access.hasAnySubscription = r["has_any_subscription"].as<bool>();
        if (!access.hasSubscription)
        {
            // 只有 AutoPDMS 授权的团队不获得平台 AI；零订阅记录的存量用户继续放行。
            if (access.hasAnySubscription)
            {
                access.allowed = false;
                access.denyMessage = "当前仅开通 AutoPDMS 授权，不含 UEAICAD 平台订阅";
            }
            return access;
        }

        access.isTrial = r["is_trial"].as<bool>();
        access.plan = r["plan"].isNull() ? "" : r["plan"].as<std::string>();
        access.endDate = r["sub_end"].as<std::string>();
        access.remainingDays = r["remaining_days"].isNull() ? 0 : r["remaining_days"].as<int>();
        access.allowed = r["sub_active"].as<bool>();
        if (!access.allowed)
        {
            // 平台/试用到期降级为免费版：仍允许 AI，使用 free 月度额度（100 积分）。
            // AutoPDMS-only 在前面 !hasSubscription 分支已拦截。
            access.plan = "free";
            access.isTrial = false;
            access.remainingDays = 0;
            access.allowed = true;
            LOG_INFO << "[Subscription] 进入免费版 AI 额度 userId=" << userId
                     << " endDate=" << access.endDate;
        }
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "CheckSubscriptionAccess 失败: " << e.base().what();
        // 查询失败时拒绝放行，避免绕过订阅限制
        access.allowed = false;
        access.denyMessage = "订阅状态查询失败，请稍后重试";
    }
    return access;
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
            having = " and (s.end_date is not null and s.end_date >= (now() at time zone 'Asia/Shanghai')::date)";
        }
        else if (expiredOnly)
        {
            having = " and (s.end_date is null or s.end_date < (now() at time zone 'Asia/Shanghai')::date)";
        }

        auto rows = db->execSqlSync(
            "select t.id, t.name, t.status, to_char(t.created_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD') as created_at, "
            "       to_char(s.start_date, 'YYYY-MM-DD') as sub_start, "
            "       to_char(s.end_date, 'YYYY-MM-DD') as sub_end, "
            "       coalesce(s.is_trial, false) as is_trial, "
            "       s.plan as sub_plan, "
            "       (s.end_date is not null and s.end_date >= (now() at time zone 'Asia/Shanghai')::date) as paid, "
            "       (s.end_date - (now() at time zone 'Asia/Shanghai')::date) as remaining_days, "
            "       exists(select 1 from team_subscription p where p.team_id = t.id "
            "              and p.product = 'pdms' and p.status = 1 "
            "              and p.end_date >= (now() at time zone 'Asia/Shanghai')::date) as pdms_active, "
            "       exists(select 1 from team_subscription x where x.team_id = t.id and x.status = 1) as has_record, "
            "       (select count(*) from team_member m where m.team_id = t.id and m.status = 1) as member_count "
            "from team t "
            "left join lateral (select start_date, end_date, plan, is_trial from team_subscription ts "
            "                   where ts.team_id = t.id and ts.status = 1 and ts.product = 'ueaicad' "
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
            t["subscription"]["plan"] = r["sub_plan"].isNull() ? "" : r["sub_plan"].as<std::string>();
            t["subscription"]["isTrial"] = r["is_trial"].as<bool>();
            t["subscription"]["remainingDays"] =
                r["remaining_days"].isNull() ? 0 : r["remaining_days"].as<int>();
            t["pdmsActive"] = r["pdms_active"].as<bool>();
            t["hasSubscriptionRecord"] = r["has_record"].as<bool>();
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
                                             const std::string &plan,
                                             const std::string &product,
                                             bool includePdms)
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

    const std::string productValue = product.empty() ? "ueaicad" : product;
    if (productValue != "pdms" && productValue != "ueaicad")
    {
        result.setResult(ApiErrorCode::ApiError_SubscriptionProductInvalid);
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
        const bool withPdms = includePdms && productValue == "ueaicad";
        db->execSqlSync(
            "insert into team_subscription (team_id, product, plan, start_date, end_date, status, updated_by, is_trial) "
            "select $1::int, $2::text, $6::text, $3::date, $4::date, 1, $5::int, false "
            "union all "
            "select $1::int, 'pdms', 'network', $3::date, $4::date, 1, $5::int, false "
            "where $2::text = 'ueaicad' and $7::boolean",
            teamId, productValue, startDate, endDate, adminUserId, planValue, withPdms);

        result.jsondata["teamId"] = teamId;
        result.jsondata["product"] = productValue;
        result.jsondata["includePdms"] = withPdms;
        result.jsondata["startDate"] = startDate;
        result.jsondata["endDate"] = endDate;
        result.jsondata["isTrial"] = false;
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
            "select t.id, t.name, t.status, t.owner_user_id, t.is_vendor, u.name as owner_name, "
            "       to_char(t.created_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD') as created_at "
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
        const bool isVendor = row["is_vendor"].as<bool>();
        result.jsondata["team"]["isVendor"] = isVendor;

        result.jsondata["subscription"] = LoadSubscription(db, teamId);
        result.jsondata["featureAuth"] = LoadFeatureAuth(db, teamId);
        auto ent = LoadProductEntitlements(db, teamId);
        result.jsondata["hasSubscriptionRecord"] = ent.hasRecord;
        result.jsondata["products"] = ent.products;
        result.jsondata["members"] = LoadTeamMembers(db, teamId, isVendor);
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

        auto vendorRows = db->execSqlSync("select is_vendor from team where id = $1", teamId);
        const bool isVendor = !vendorRows.empty() && vendorRows[0]["is_vendor"].as<bool>();
        Json::Value members = LoadTeamMembers(db, teamId, isVendor);
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

// ---------------------------------------------------------------- 厂商团队管理员（跨团队）

namespace {
void SetVendorDenied(HttpResult &result)
{
    result.setResult(ApiErrorCode::ApiError_AdminPrivilegeRequired,
                     "只有厂商团队的团队管理员或超级管理员可以管理其他团队");
}
} // namespace

HttpResult TeamService::VendorListTeams(int operatorUserId)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        int ownTeamId = 0;
        std::string roleCode;
        if (!IsVendorOperator(db, operatorUserId, ownTeamId, roleCode))
        {
            SetVendorDenied(result);
            return result;
        }

        auto rows = db->execSqlSync(
            "select t.id, t.name, t.status, t.is_vendor, t.is_personal, t.owner_user_id, "
            "       to_char(t.created_at AT TIME ZONE 'Asia/Shanghai', 'YYYY-MM-DD') as created_at, "
            "       (select count(*) from team_member m where m.team_id = t.id and m.status = 1) as member_count "
            "from team t order by t.is_vendor desc, t.id");
        Json::Value teams(Json::arrayValue);
        for (const auto &row : rows)
        {
            Json::Value team;
            team["id"] = row["id"].as<int>();
            team["name"] = row["name"].as<std::string>();
            team["status"] = row["status"].as<int>();
            team["isVendor"] = row["is_vendor"].as<bool>();
            team["isPersonal"] = row["is_personal"].as<bool>();
            team["ownerUserId"] = row["owner_user_id"].isNull() ? 0 : row["owner_user_id"].as<int>();
            team["createdAt"] = row["created_at"].as<std::string>();
            team["memberCount"] = static_cast<int>(row["member_count"].as<int64_t>());
            teams.append(team);
        }

        result.jsondata["teams"] = teams;
        result.jsondata["ownTeamId"] = ownTeamId;
        result.setResult(ApiErrorCode::ApiError_Success, "success");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "VendorListTeams 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult TeamService::VendorGetTeam(int operatorUserId, int teamId)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        int ownTeamId = 0;
        std::string roleCode;
        if (!IsVendorOperator(db, operatorUserId, ownTeamId, roleCode))
        {
            SetVendorDenied(result);
            return result;
        }
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "VendorGetTeam 校验操作者失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
        return result;
    }
    return AdminGetTeam(teamId);
}

HttpResult TeamService::VendorCreateMember(int operatorUserId,
                                           int teamId,
                                           const std::string &userName,
                                           const std::string &password,
                                           const std::vector<std::string> &roleCodes,
                                           const std::vector<std::string> &systemRoleCodes,
                                           const std::string &nickName)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        int ownTeamId = 0;
        std::string roleCode;
        if (!IsVendorOperator(db, operatorUserId, ownTeamId, roleCode))
        {
            SetVendorDenied(result);
            return result;
        }
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "VendorCreateMember 校验操作者失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
        return result;
    }
    return CreateMemberInTeam(operatorUserId, teamId, userName, password, roleCodes,
                              systemRoleCodes, nickName);
}

HttpResult TeamService::VendorUpdateMemberRoles(int operatorUserId,
                                                int teamId,
                                                int memberUserId,
                                                const std::vector<std::string> &roleCodes,
                                                const std::vector<std::string> &systemRoleCodes,
                                                bool updateSystemRoles)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        int ownTeamId = 0;
        std::string roleCode;
        if (!IsVendorOperator(db, operatorUserId, ownTeamId, roleCode))
        {
            SetVendorDenied(result);
            return result;
        }
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "VendorUpdateMemberRoles 校验操作者失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
        return result;
    }
    return UpdateMemberRolesInTeam(operatorUserId, teamId, memberUserId, roleCodes,
                                   systemRoleCodes, updateSystemRoles);
}

HttpResult TeamService::VendorRemoveMember(int operatorUserId, int teamId, int memberUserId)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        int ownTeamId = 0;
        std::string roleCode;
        if (!IsVendorOperator(db, operatorUserId, ownTeamId, roleCode))
        {
            SetVendorDenied(result);
            return result;
        }
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "VendorRemoveMember 校验操作者失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
        return result;
    }
    return RemoveMemberInTeam(operatorUserId, teamId, memberUserId);
}

HttpResult TeamService::RemoveMember(int operatorUserId, int memberUserId)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    int teamId = 0;
    try
    {
        std::string roleCode;
        if (!GetMembership(db, operatorUserId, teamId, roleCode))
        {
            result.setResult(ApiErrorCode::ApiError_NotTeamMember);
            return result;
        }
        if (!IsTeamAdminOf(db, operatorUserId, teamId))
        {
            result.setResult(ApiErrorCode::ApiError_NotTeamAdmin);
            return result;
        }
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "RemoveMember 校验操作者失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
        return result;
    }
    return RemoveMemberInTeam(operatorUserId, teamId, memberUserId);
}

HttpResult TeamService::RemoveMemberInTeam(int operatorUserId, int teamId, int memberUserId)
{
    HttpResult result;
    auto db = drogon::app().getDbClient();
    try
    {
        if (memberUserId == operatorUserId)
        {
            result.setResult(ApiErrorCode::ApiError_InvalidOperation, "不能移除自己");
            return result;
        }

        auto teamRows = db->execSqlSync(
            "select id, owner_user_id, is_personal from team where id = $1", teamId);
        if (teamRows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_TeamNotFound);
            return result;
        }
        if (teamRows[0]["is_personal"].as<bool>())
        {
            result.setResult(ApiErrorCode::ApiError_InvalidOperation, "个人团队没有可移除的成员");
            return result;
        }
        const int ownerUserId = teamRows[0]["owner_user_id"].isNull()
                                    ? 0
                                    : teamRows[0]["owner_user_id"].as<int>();
        if (ownerUserId == memberUserId)
        {
            result.setResult(ApiErrorCode::ApiError_TeamOwnerProtected);
            return result;
        }

        auto memberRows = db->execSqlSync(
            "select u.name from team_member tm join \"user\" u on u.id = tm.user_id "
            "where tm.team_id = $1 and tm.user_id = $2",
            teamId, memberUserId);
        if (memberRows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_NotTeamMember, "该账号不是这个团队的成员");
            return result;
        }
        const std::string memberName = memberRows[0]["name"].as<std::string>();

        if (IsSuperAdmin(db, memberUserId) && !HasAdminBit(db, operatorUserId))
        {
            result.setResult(ApiErrorCode::ApiError_AdminPrivilegeRequired,
                             "只有持有 Admin 位的账号可以移除超级管理员");
            return result;
        }
        if (IsTeamAdminOf(db, memberUserId, teamId) && CountTeamAdmins(db, teamId) <= 1)
        {
            result.setResult(ApiErrorCode::ApiError_NotTeamAdmin, "团队至少保留一个团队管理员");
            return result;
        }

        int personalTeamId = 0;
        if (!ResolvePersonalTeam(db, memberUserId, memberName, personalTeamId))
        {
            result.setResult(ApiErrorCode::ApiError_InternalError, "开通个人团队失败");
            return result;
        }
        if (personalTeamId == teamId)
        {
            result.setResult(ApiErrorCode::ApiError_InvalidOperation, "该成员已在个人团队");
            return result;
        }

        auto adminRoleRows = db->execSqlSync(
            "select id from role where code = 'team_admin' and scope = 2");
        if (adminRoleRows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_InvalidRoleCode, "缺少 team_admin 角色");
            return result;
        }
        std::vector<int> roleIds{adminRoleRows[0]["id"].as<int>()};

        std::string moveErr;
        if (!MoveMemberToTeam(db, memberUserId, personalTeamId, roleIds, moveErr))
        {
            LOG_ERROR << "VendorRemoveMember 移动失败: " << moveErr;
            result.setResult(ApiErrorCode::ApiError_TeamMemberCreateFailure, moveErr);
            return result;
        }

        auto personalRows = db->execSqlSync("select name from team where id = $1", personalTeamId);
        result.jsondata["userId"] = memberUserId;
        result.jsondata["teamId"] = personalTeamId;
        result.jsondata["teamName"] = personalRows.empty()
                                          ? std::string()
                                          : personalRows[0]["name"].as<std::string>();
        result.setResult(ApiErrorCode::ApiError_Success, "成员已移回个人团队");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "VendorRemoveMember 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult TeamService::VendorMoveMembers(int operatorUserId,
                                          int teamId,
                                          const std::vector<int> &userIds,
                                          const std::vector<std::string> &roleCodes)
{
    HttpResult result;
    if (userIds.empty())
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "请选择要移动的用户");
        return result;
    }
    if (roleCodes.empty())
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "至少选择一个团队角色");
        return result;
    }

    auto db = drogon::app().getDbClient();
    try
    {
        int ownTeamId = 0;
        std::string roleCode;
        if (!IsVendorOperator(db, operatorUserId, ownTeamId, roleCode))
        {
            SetVendorDenied(result);
            return result;
        }

        auto teamRows = db->execSqlSync(
            "select status, is_personal from team where id = $1", teamId);
        if (teamRows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_TeamNotFound);
            return result;
        }
        if (teamRows[0]["status"].as<int>() != 1)
        {
            result.setResult(ApiErrorCode::ApiError_InvalidOperation, "团队已停用，不能移入成员");
            return result;
        }
        if (teamRows[0]["is_personal"].as<bool>())
        {
            result.setResult(ApiErrorCode::ApiError_InvalidOperation, "个人团队不能移入其他成员");
            return result;
        }

        std::vector<int> roleIds;
        if (!ResolveTeamRoleIds(db, roleCodes, roleIds))
        {
            result.setResult(ApiErrorCode::ApiError_InvalidRoleCode);
            return result;
        }
        if (HasTeamAdminRole(roleCodes) && !CanGrantTeamAdmin(db, operatorUserId, teamId))
        {
            result.setResult(ApiErrorCode::ApiError_NotTeamAdmin,
                             "只有厂商超级管理员或团队所有者可以授予团队管理员");
            return result;
        }

        Json::Value results(Json::arrayValue);
        int moved = 0;
        int failed = 0;
        for (const int targetUserId : userIds)
        {
            Json::Value item;
            item["userId"] = targetUserId;
            item["ok"] = false;

            auto userRows = db->execSqlSync(
                "select name from \"user\" where id = $1", targetUserId);
            if (userRows.empty())
            {
                item["userName"] = "";
                item["message"] = "用户不存在";
                failed++;
                results.append(item);
                continue;
            }
            const std::string targetName = userRows[0]["name"].as<std::string>();
            item["userName"] = targetName;

            auto memberRows = db->execSqlSync(
                "select tm.team_id, t.is_personal, t.owner_user_id "
                "from team_member tm join team t on t.id = tm.team_id "
                "where tm.user_id = $1",
                targetUserId);
            if (memberRows.empty())
            {
                item["message"] = "该账号不属于任何团队";
                failed++;
                results.append(item);
                continue;
            }
            const int currentTeamId = memberRows[0]["team_id"].as<int>();
            const bool currentIsPersonal = memberRows[0]["is_personal"].as<bool>();
            const int currentOwnerId = memberRows[0]["owner_user_id"].isNull()
                                           ? 0
                                           : memberRows[0]["owner_user_id"].as<int>();
            if (currentTeamId == teamId)
            {
                item["message"] = "该账号已在目标团队";
                failed++;
                results.append(item);
                continue;
            }
            if (!currentIsPersonal && currentOwnerId == targetUserId)
            {
                item["message"] = "该账号是所在团队的创建者，不能直接移动";
                failed++;
                results.append(item);
                continue;
            }
            if (IsSuperAdmin(db, targetUserId) && !HasAdminBit(db, operatorUserId))
            {
                item["message"] = "只有持有 Admin 位的账号可以移动超级管理员";
                failed++;
                results.append(item);
                continue;
            }

            std::string moveErr;
            if (!MoveMemberToTeam(db, targetUserId, teamId, roleIds, moveErr))
            {
                item["message"] = moveErr.empty() ? "移动失败" : moveErr;
                failed++;
            }
            else
            {
                item["ok"] = true;
                item["message"] = "已移入";
                moved++;
            }
            results.append(item);
        }

        result.jsondata["moved"] = moved;
        result.jsondata["failed"] = failed;
        result.jsondata["results"] = results;
        result.setResult(ApiErrorCode::ApiError_Success, "移动完成");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "VendorMoveMembers 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult TeamService::VendorListCandidates(int operatorUserId,
                                             const std::string &keyword,
                                             int excludeTeamId,
                                             int limit)
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
        int ownTeamId = 0;
        std::string roleCode;
        if (!IsVendorOperator(db, operatorUserId, ownTeamId, roleCode))
        {
            SetVendorDenied(result);
            return result;
        }

        auto rows = db->execSqlSync(
            "select u.id, u.name, u.nick_name, u.status, tm.team_id, t.name as team_name, "
            "       r.code as role_code, r.name as role_name "
            "from \"user\" u "
            "left join team_member tm on tm.user_id = u.id "
            "left join team t on t.id = tm.team_id "
            "left join role r on r.id = tm.role_id "
            "where coalesce(u.status, 1) = 1 "
            "  and (tm.team_id is null or tm.team_id <> $2::int) "
            "  and ($1::text = '' or u.name ilike '%' || $1 || '%' "
            "       or coalesce(u.nick_name, '') ilike '%' || $1 || '%') "
            "order by u.id limit $3::int",
            keyword, excludeTeamId, limit);

        Json::Value users(Json::arrayValue);
        for (const auto &r : rows)
        {
            Json::Value u;
            u["userId"] = r["id"].as<int>();
            u["userName"] = r["name"].as<std::string>();
            u["nickName"] = r["nick_name"].isNull() ? "" : r["nick_name"].as<std::string>();
            u["status"] = r["status"].isNull() ? 1 : r["status"].as<int>();
            u["teamId"] = r["team_id"].isNull() ? 0 : r["team_id"].as<int>();
            u["teamName"] = r["team_name"].isNull() ? "" : r["team_name"].as<std::string>();
            u["roleCode"] = r["role_code"].isNull() ? "" : r["role_code"].as<std::string>();
            u["roleName"] = r["role_name"].isNull() ? "" : r["role_name"].as<std::string>();
            users.append(u);
        }
        result.jsondata["users"] = users;
        result.setResult(ApiErrorCode::ApiError_Success, "success");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "VendorListCandidates 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

HttpResult TeamService::RenameTeam(int operatorUserId, const std::string &name)
{
    HttpResult result;
    const std::string teamName = TrimText(name);
    if (teamName.empty() || teamName.size() > 64)
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "团队名称不能为空且不超过 64 个字符");
        return result;
    }

    auto db = drogon::app().getDbClient();
    try
    {
        int teamId = 0;
        std::string roleCode;
        if (!GetMembership(db, operatorUserId, teamId, roleCode))
        {
            result.setResult(ApiErrorCode::ApiError_NotTeamMember);
            return result;
        }
        if (!IsTeamAdminOf(db, operatorUserId, teamId))
        {
            result.setResult(ApiErrorCode::ApiError_NotTeamAdmin);
            return result;
        }

        auto dupRows = db->execSqlSync("select 1 from team where name = $1 and id <> $2",
                                       teamName, teamId);
        if (!dupRows.empty())
        {
            result.setResult(ApiErrorCode::ApiError_CodeAlreadyExists, "团队名称已存在");
            return result;
        }
        db->execSqlSync("update team set name = $2 where id = $1", teamId, teamName);

        result.jsondata["teamId"] = teamId;
        result.jsondata["name"] = teamName;
        result.setResult(ApiErrorCode::ApiError_Success, "团队名称已更新");
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_ERROR << "RenameTeam 失败: " << e.base().what();
        result.setResult(ApiErrorCode::ApiError_DatabaseError);
    }
    return result;
}

} // namespace Services
} // namespace UEAdminAPI

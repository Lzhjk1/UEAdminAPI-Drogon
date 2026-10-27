-- 优易公司（厂商团队）+ 8 个厂商账号设为超级管理员
--
-- 前置
--   1) db/migrations/0002_billing_team.sql、0003_super_admin.sql 已在目标库应用；
--   2) 后端已升级到含 TeamController / AdminController 的版本，否则数据在库里、
--      前端调用 /api/team、/api/admin/* 仍是 404。
--
-- 幂等
--   可重复执行。所有动作以「优易公司」这个团队名和下面 8 个 user id 为锚点，
--   其余团队与账号不受影响。
--
-- 执行
--   psql -v ON_ERROR_STOP=1 -f db/ops/2026-10-27_youyi_company_and_superadmins.sql

BEGIN;

DO $$
BEGIN
    IF to_regclass('public.team') IS NULL
        OR to_regclass('public.team_member') IS NULL
        OR to_regclass('public.user_system_role') IS NULL THEN
        RAISE EXCEPTION '团队/RBAC 表不存在，请先应用 0002、0003 迁移';
    END IF;

    IF (SELECT count(*) FROM role WHERE code IN ('super_admin', 'team_admin')) < 2 THEN
        RAISE EXCEPTION '缺少 super_admin / team_admin 角色，请先应用 0002、0003 迁移';
    END IF;

    IF (SELECT count(*) FROM "user" WHERE id IN (1, 45, 46, 47, 48, 49, 50, 51)) <> 8 THEN
        RAISE EXCEPTION '厂商账号清单与库中记录不符（应有 8 个），请先核对 user 表';
    END IF;

    IF NOT EXISTS (SELECT 1 FROM "user" WHERE id = 46) THEN
        RAISE EXCEPTION 'owner 账号 id=46（uesoft）不存在';
    END IF;
END $$;

-- 厂商账号清单：owner 为 46（uesoft）
CREATE TEMP TABLE _vendor_users (user_id integer PRIMARY KEY) ON COMMIT DROP;
INSERT INTO _vendor_users (user_id)
VALUES (1), (45), (46), (47), (48), (49), (50), (51);

-- 1) 厂商团队
INSERT INTO team (name, owner_user_id, status)
VALUES ('优易公司', 46, 1)
ON CONFLICT (name) DO UPDATE
    SET owner_user_id = EXCLUDED.owner_user_id,
        status        = 1;

-- 2) 退出迁移 0002 自动创建的个人团队（Team-<id>），并清掉这些空壳
--    只认「owner 是本人且名字恰好为 Team-<id>」的团队，避免误删真实团队
CREATE TEMP TABLE _personal_teams ON COMMIT DROP AS
SELECT t.id
FROM team t
         JOIN _vendor_users v ON v.user_id = t.owner_user_id
WHERE t.name = 'Team-' || t.owner_user_id::text;

DELETE
FROM team_member
WHERE user_id IN (SELECT user_id FROM _vendor_users)
  AND team_id IN (SELECT id FROM _personal_teams);

DELETE FROM team_feature_auth WHERE team_id IN (SELECT id FROM _personal_teams);
DELETE FROM team_subscription WHERE team_id IN (SELECT id FROM _personal_teams);
DELETE FROM team_member WHERE team_id IN (SELECT id FROM _personal_teams);
DELETE FROM team WHERE id IN (SELECT id FROM _personal_teams);

-- 3) 8 个账号全部纳入优易公司；都是超管，团队内统一给团队管理员
INSERT INTO team_member (team_id, user_id, role_id, status)
SELECT t.id, v.user_id, r.id, 1
FROM team t
         CROSS JOIN _vendor_users v
         JOIN role r ON r.code = 'team_admin'
WHERE t.name = '优易公司'
ON CONFLICT (user_id) DO UPDATE
    SET team_id = EXCLUDED.team_id,
        role_id = EXCLUDED.role_id,
        status  = 1;

-- 4) 厂商团队开通云端工程 / 在线更新
INSERT INTO team_feature_auth (team_id, feature_code, enabled)
SELECT t.id, f.code, true
FROM team t
         CROSS JOIN (VALUES ('cloud_project'), ('online_update')) AS f(code)
WHERE t.name = '优易公司'
ON CONFLICT (team_id, feature_code) DO UPDATE
    SET enabled    = EXCLUDED.enabled,
        updated_at = now();

-- 5) 厂商团队不按付费期展示「已过期」；不需要这段就删掉本节
INSERT INTO team_subscription (team_id, plan, start_date, end_date, status)
SELECT t.id, 'vendor', CURRENT_DATE, DATE '2099-12-31', 1
FROM team t
WHERE t.name = '优易公司'
  AND NOT EXISTS (SELECT 1
                  FROM team_subscription s
                  WHERE s.team_id = t.id
                    AND s.status = 1
                    AND s.end_date >= CURRENT_DATE);

-- 6) 超管：系统角色 + Admin 位
INSERT INTO user_system_role (user_id, role_id)
SELECT v.user_id, r.id
FROM _vendor_users v
         CROSS JOIN role r
WHERE r.code = 'super_admin'
ON CONFLICT DO NOTHING;

-- Admin 位是兜底能力：/api/oauth2/introspect 只按 privilege 位输出 roles，
-- 不置位的话登录器侧拿到的仍是普通用户；角色本身仍以 user_system_role 为准。
UPDATE "user"
SET privilege = privilege | 4
WHERE id IN (SELECT user_id FROM _vendor_users)
  AND (privilege & 4) = 0;

INSERT INTO user_balance (user_id)
SELECT user_id FROM _vendor_users
ON CONFLICT (user_id) DO NOTHING;

-- 7) 校验：团队、成员与角色
SELECT t.id AS team_id, t.name, t.owner_user_id, t.status, count(tm.user_id) AS members
FROM team t
         LEFT JOIN team_member tm ON tm.team_id = t.id
WHERE t.name = '优易公司'
GROUP BY t.id, t.name, t.owner_user_id, t.status;

SELECT u.id, u.name, u.privilege, t.name AS team, r.code AS team_role
FROM "user" u
         LEFT JOIN team_member tm ON tm.user_id = u.id
         LEFT JOIN team t ON t.id = tm.team_id
         LEFT JOIN role r ON r.id = tm.role_id
WHERE u.id IN (SELECT user_id FROM _vendor_users)
ORDER BY u.id;

SELECT u.id, u.name, sr.code AS system_role
FROM user_system_role usr
         JOIN "user" u ON u.id = usr.user_id
         JOIN role sr ON sr.id = usr.role_id
WHERE sr.code = 'super_admin'
ORDER BY u.id;

COMMIT;

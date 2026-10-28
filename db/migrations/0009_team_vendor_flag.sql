-- 0009 标记厂商团队（优易公司）
--
-- 背景
--   「超级管理员」是 scope=1 的系统角色，只能在厂商团队里配置；其他团队只有团队角色。
--   用一个显式标记代替硬编码团队名/owner，后续厂商团队变更只需改这一列。
--
-- 幂等
--   ADD COLUMN IF NOT EXISTS；回填只把优易公司（或其 owner uesoft）置 true。

ALTER TABLE team ADD COLUMN IF NOT EXISTS is_vendor boolean NOT NULL DEFAULT false;

UPDATE team
SET is_vendor = true
WHERE name = '优易公司'
   OR owner_user_id IN (SELECT id FROM "user" WHERE name = 'uesoft');

COMMENT ON COLUMN team.is_vendor IS '厂商团队标记：只有该团队可以配置系统角色 super_admin';

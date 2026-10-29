-- 0010 标记个人团队
--
-- 背景
--   注册时会为每个用户自动创建 "Team-{userId}-{userName}" 个人团队。
--   团队管理允许团队管理员改自己团队名之后，不能再靠名字识别个人团队，
--   否则「移除成员」会再造一个同名团队。改为显式列。
--
-- 幂等
--   ADD COLUMN IF NOT EXISTS；回填只认旧的自动命名规则。

ALTER TABLE team ADD COLUMN IF NOT EXISTS is_personal boolean NOT NULL DEFAULT false;

UPDATE team t
SET is_personal = true
WHERE t.name ~ '^Team-[0-9]+-'
  AND t.owner_user_id IS NOT NULL;

COMMENT ON COLUMN team.is_personal IS '个人团队标记：注册时自动创建，成员被移出团队时回到这里';

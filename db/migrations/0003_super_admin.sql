-- 0003 超级管理员（厂商侧系统角色）
--
-- 目的
--   厂商需要一个「看得到、也改得了所有团队数据」的角色。此前厂商身份只有
--   user.privilege 的 Admin 位（DB 级），无法在 RBAC 里表达，也无法只授予某个账号。
--
-- 约定
--   role.scope = 1 表示系统角色（不隶属于某个团队），scope = 2 表示团队角色。
--   系统角色通过 user_system_role 直接挂到账号上；团队角色仍走 team_member.role_id，
--   因此厂商账号不必为了拿系统角色而加入某个团队，也不会占用团队里的唯一角色位。
--   本文件在存量库与空库上都要能执行，全部增量 + ON CONFLICT 幂等。

CREATE TABLE IF NOT EXISTS user_system_role
(
    user_id    integer NOT NULL REFERENCES "user" (id),
    role_id    integer NOT NULL REFERENCES role (id),
    granted_by integer,
    created_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (user_id, role_id)
);
CREATE INDEX IF NOT EXISTS idx_user_system_role_role ON user_system_role (role_id);

INSERT INTO role (code, name, scope)
VALUES ('super_admin', '超级管理员', 1)
ON CONFLICT (code) DO NOTHING;

-- 超级管理员本期等价于厂商的全量后台能力：授予全部已登记权限
INSERT INTO role_permission (role_id, permission_id)
SELECT r.id, p.id
FROM role r
         CROSS JOIN permission p
WHERE r.code = 'super_admin'
ON CONFLICT DO NOTHING;

-- 存量厂商账号（privilege 的 Admin 位）补一条系统角色，便于在角色页里看清归属。
-- 回填只补角色，不回写 privilege：Admin 位仍是厂商的兜底能力，避免授予角色被回收后无法登录后台。
INSERT INTO user_system_role (user_id, role_id)
SELECT u.id, r.id
FROM "user" u
         JOIN role r ON r.code = 'super_admin'
WHERE (coalesce(u.privilege, 0) & 4) <> 0
ON CONFLICT DO NOTHING;

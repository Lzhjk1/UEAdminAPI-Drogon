-- 0004 团队成员多角色
--
-- 背景
--   原来 team_member.role_id 是单角色。产品要求同一个人可以同时拥有多个团队角色
--   （例如 工程师 + 项目经理），因此新增 team_member_role 关联表作为**权威来源**。
--
-- 兼容
--   team_member.role_id 保留为「主角色」（= 该成员角色中的第一个，team_admin 优先），
--   让旧版后端/旧版客户端在滚动发布期间仍能按单角色工作；新代码写入时两处同步更新。
--
-- 幂等
--   表用 IF NOT EXISTS，回填用 ON CONFLICT DO NOTHING；存量库与空库都能执行。

CREATE TABLE IF NOT EXISTS team_member_role
(
    team_member_id integer NOT NULL REFERENCES team_member (id) ON DELETE CASCADE,
    role_id        integer NOT NULL REFERENCES role (id),
    created_at     timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (team_member_id, role_id)
);
CREATE INDEX IF NOT EXISTS idx_team_member_role_role ON team_member_role (role_id);

-- 把现有单角色回填进关联表
INSERT INTO team_member_role (team_member_id, role_id)
SELECT tm.id, tm.role_id
FROM team_member tm
WHERE tm.role_id IS NOT NULL
ON CONFLICT DO NOTHING;

-- 0002 计费 / 团队 / 授权 / 安装包版本
--
-- 约定
--   金额一律 bigint 存「分」；日期用 date（付费期），时间用 timestamptz。
--   privilege 位标志（utils/EnumUserPrivileges.h）继续表示厂商侧身份：
--     Default=1 / User=2 / Admin=4，其中 Admin 即「厂商超管」。
--   团队角色走 role.code（team_admin / engineer / pm），与 privilege 正交。
--   本文件在存量库与空库上都要能执行，全部增量 + ON CONFLICT 幂等。

-- ---------------------------------------------------------------- user 增量列
ALTER TABLE "user" ADD COLUMN IF NOT EXISTS status          smallint NOT NULL DEFAULT 1;  -- 1 正常 2 禁用
ALTER TABLE "user" ADD COLUMN IF NOT EXISTS must_change_pwd boolean  NOT NULL DEFAULT false;
ALTER TABLE "user" ADD COLUMN IF NOT EXISTS pwd_updated_at  timestamptz;

-- ---------------------------------------------------------------- 团队
CREATE TABLE IF NOT EXISTS team
(
    id            serial PRIMARY KEY,
    name          text NOT NULL UNIQUE,
    owner_user_id integer NOT NULL REFERENCES "user" (id),
    status        smallint NOT NULL DEFAULT 1,  -- 1 正常 2 停用
    created_at    timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_team_owner ON team (owner_user_id);

-- 一个用户只能属于一个团队：user_id 唯一
CREATE TABLE IF NOT EXISTS team_member
(
    id         serial PRIMARY KEY,
    team_id    integer NOT NULL REFERENCES team (id),
    user_id    integer NOT NULL UNIQUE REFERENCES "user" (id),
    role_id    integer NOT NULL,
    status     smallint NOT NULL DEFAULT 1,  -- 1 正常 2 停用
    created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_team_member_team ON team_member (team_id);

-- ---------------------------------------------------------------- RBAC
CREATE TABLE IF NOT EXISTS role
(
    id    serial PRIMARY KEY,
    code  text NOT NULL UNIQUE,
    name  text NOT NULL,
    scope smallint NOT NULL DEFAULT 2  -- 1 系统 2 团队
);

CREATE TABLE IF NOT EXISTS permission
(
    id     serial PRIMARY KEY,
    code   text NOT NULL UNIQUE,
    module text NOT NULL,
    action text NOT NULL
);

CREATE TABLE IF NOT EXISTS permission_group
(
    id   serial PRIMARY KEY,
    name text NOT NULL UNIQUE
);

CREATE TABLE IF NOT EXISTS group_permission
(
    group_id      integer NOT NULL REFERENCES permission_group (id) ON DELETE CASCADE,
    permission_id integer NOT NULL REFERENCES permission (id) ON DELETE CASCADE,
    PRIMARY KEY (group_id, permission_id)
);

CREATE TABLE IF NOT EXISTS role_permission
(
    role_id       integer NOT NULL REFERENCES role (id) ON DELETE CASCADE,
    permission_id integer NOT NULL REFERENCES permission (id) ON DELETE CASCADE,
    PRIMARY KEY (role_id, permission_id)
);

-- team_member.role_id 外键在 role 之后补（空库顺序依赖）
DO
$$
    BEGIN
        IF NOT EXISTS (SELECT 1
                       FROM pg_constraint
                       WHERE conname = 'fk_team_member_role') THEN
            ALTER TABLE team_member
                ADD CONSTRAINT fk_team_member_role FOREIGN KEY (role_id) REFERENCES role (id);
        END IF;
    END
$$;

-- ---------------------------------------------------------------- 付费与授权
-- 历史保留，取 end_date 最大的一条为当前订阅
CREATE TABLE IF NOT EXISTS team_subscription
(
    id         serial PRIMARY KEY,
    team_id    integer NOT NULL REFERENCES team (id),
    plan       text,
    start_date date    NOT NULL,
    end_date   date    NOT NULL,
    status     smallint NOT NULL DEFAULT 1,  -- 1 有效 2 作废
    updated_by integer,
    updated_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_team_subscription_team ON team_subscription (team_id, end_date DESC);

-- 厂商可控的云端功能开关（缺失即视为关闭）
CREATE TABLE IF NOT EXISTS team_feature_auth
(
    id           serial PRIMARY KEY,
    team_id      integer NOT NULL REFERENCES team (id),
    feature_code text    NOT NULL,  -- cloud_project / online_update
    enabled      boolean NOT NULL DEFAULT false,
    updated_by   integer,
    updated_at   timestamptz NOT NULL DEFAULT now(),
    UNIQUE (team_id, feature_code)
);

-- 账号级开关，覆盖团队设置
CREATE TABLE IF NOT EXISTS user_feature_auth
(
    id           serial PRIMARY KEY,
    user_id      integer NOT NULL REFERENCES "user" (id),
    feature_code text    NOT NULL,
    enabled      boolean NOT NULL DEFAULT false,
    updated_at   timestamptz NOT NULL DEFAULT now(),
    UNIQUE (user_id, feature_code)
);

-- ---------------------------------------------------------------- 余额与流水
CREATE TABLE IF NOT EXISTS user_balance
(
    user_id       integer PRIMARY KEY REFERENCES "user" (id),
    balance_cents bigint NOT NULL DEFAULT 0 CHECK (balance_cents >= 0),
    updated_at    timestamptz NOT NULL DEFAULT now()
);

-- type: 1 充值 2 消费 3 退款；amount_cents 恒为正，方向由 type 决定
CREATE TABLE IF NOT EXISTS balance_txn
(
    id            bigserial PRIMARY KEY,
    user_id       integer NOT NULL REFERENCES "user" (id),
    type          smallint NOT NULL,
    amount_cents  bigint   NOT NULL,
    balance_after bigint   NOT NULL,
    order_id      integer,
    ext_ref       text UNIQUE,
    remark        text,
    created_at    timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_balance_txn_user ON balance_txn (user_id, created_at DESC);

-- ---------------------------------------------------------------- 支付订单
CREATE TABLE IF NOT EXISTS payment_order
(
    id                bigserial PRIMARY KEY,
    order_no          text    NOT NULL UNIQUE,
    user_id           integer NOT NULL REFERENCES "user" (id),
    amount_cents      bigint  NOT NULL,
    channel           smallint NOT NULL,  -- 1 支付宝 2 微信 9 模拟
    status            smallint NOT NULL DEFAULT 0,  -- 0 待支付 1 已支付 2 已关闭
    qr_content        text,
    provider_trade_no text,
    idem_key          text UNIQUE,
    expire_at         timestamptz,
    paid_at           timestamptz,
    created_at        timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_payment_order_user ON payment_order (user_id, created_at DESC);

CREATE TABLE IF NOT EXISTS payment_callback_log
(
    id         bigserial PRIMARY KEY,
    order_no   text,
    channel    smallint,
    raw_body   text,
    headers    jsonb,
    verified   boolean,
    created_at timestamptz NOT NULL DEFAULT now()
);

-- ---------------------------------------------------------------- 安装包版本
CREATE TABLE IF NOT EXISTS installer_version
(
    id           serial PRIMARY KEY,
    version      text   NOT NULL,
    build_no     integer,
    file_name    text   NOT NULL,
    file_path    text   NOT NULL,
    size_bytes   bigint,
    sha256       text   NOT NULL,
    notes        text,
    status       smallint NOT NULL DEFAULT 0,  -- 0 草稿 1 已发布 2 下线
    min_version  text,
    published_at timestamptz,
    created_at   timestamptz NOT NULL DEFAULT now(),
    UNIQUE (version)
);

-- ---------------------------------------------------------------- 基础数据
INSERT INTO role (code, name, scope)
VALUES ('team_admin', '团队管理员', 2),
       ('engineer', '工程师', 2),
       ('pm', '项目经理', 2)
ON CONFLICT (code) DO NOTHING;

INSERT INTO permission (code, module, action)
VALUES ('cloud_project', 'cloud', 'use'),
       ('online_update', 'update', 'use'),
       ('team_manage', 'team', 'manage'),
       ('billing_view', 'bill', 'view')
ON CONFLICT (code) DO NOTHING;

-- 团队管理员：全部权限
INSERT INTO role_permission (role_id, permission_id)
SELECT r.id, p.id
FROM role r
         CROSS JOIN permission p
WHERE r.code = 'team_admin'
ON CONFLICT DO NOTHING;

-- 工程师 / 项目经理：云端功能 + 更新 + 账单查看，不含成员管理
INSERT INTO role_permission (role_id, permission_id)
SELECT r.id, p.id
FROM role r
         JOIN permission p ON p.code IN ('cloud_project', 'online_update', 'billing_view')
WHERE r.code IN ('engineer', 'pm')
ON CONFLICT DO NOTHING;

INSERT INTO permission_group (name)
VALUES ('基础权限组')
ON CONFLICT (name) DO NOTHING;

INSERT INTO group_permission (group_id, permission_id)
SELECT g.id, p.id
FROM permission_group g
         JOIN permission p ON p.code IN ('cloud_project', 'online_update', 'billing_view')
WHERE g.name = '基础权限组'
ON CONFLICT DO NOTHING;

-- ---------------------------------------------------------------- 存量数据回填
-- 每个存量用户一个专属团队，自己当团队管理员（仅建一次）
INSERT INTO team (name, owner_user_id, status)
SELECT 'Team-' || u.id, u.id, 1
FROM "user" u
WHERE NOT EXISTS (SELECT 1 FROM team t WHERE t.owner_user_id = u.id);

INSERT INTO team_member (team_id, user_id, role_id)
SELECT t.id, t.owner_user_id, r.id
FROM team t
         JOIN role r ON r.code = 'team_admin'
ON CONFLICT (user_id) DO NOTHING;

INSERT INTO user_balance (user_id)
SELECT u.id FROM "user" u
ON CONFLICT (user_id) DO NOTHING;

-- 存量团队保持现有云端能力（升级不应立刻中断老客户的云端工程/更新）
INSERT INTO team_feature_auth (team_id, feature_code, enabled)
SELECT t.id, f.code, true
FROM team t
         CROSS JOIN (VALUES ('cloud_project'), ('online_update')) AS f(code)
ON CONFLICT (team_id, feature_code) DO NOTHING;

-- 付费期不自动造数据：由上架后的厂商控制台按合同逐团队开通。
-- 若需要给存量客户一次性宽限，执行（自行确认周期）：
-- INSERT INTO team_subscription (team_id, plan, start_date, end_date, status)
-- SELECT t.id, 'legacy', CURRENT_DATE, CURRENT_DATE + INTERVAL '365 days', 1 FROM team t;

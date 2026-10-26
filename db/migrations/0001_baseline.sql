-- 0001 baseline：现有 8 张表的基线结构
--
-- 说明
--   1. 本文件按 models/*.h 的列类型反推而来（仓库此前没有任何 DDL），
--      用于「空库能一次建全」与 CI；生产库已存在这些表，全部语句都是
--      IF NOT EXISTS，因此在生产库上执行等价于 no-op。
--   2. 如拿到生产库权限，建议用 db/dump_baseline.sh 重新生成一份权威基线
--      （在应用 0002 之前导出），替换本文件。
--   3. 字符串列统一用 text：drogon 模型只区分 std::string，不携带 varchar 长度。

CREATE TABLE IF NOT EXISTS "user"
(
    id               serial PRIMARY KEY,
    name             text      NOT NULL,
    password_salt    bytea     NOT NULL,
    password_hash    bytea     NOT NULL,
    privilege        integer   NOT NULL DEFAULT 1,
    nick_name        text,
    telephone_number text,
    email            text,
    is_male          boolean,
    create_at        timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_user_name ON "user" (name);
CREATE INDEX IF NOT EXISTS idx_user_email ON "user" (email);
CREATE INDEX IF NOT EXISTS idx_user_phone ON "user" (telephone_number);

CREATE TABLE IF NOT EXISTS user_flashtoken
(
    user_id          integer PRIMARY KEY,
    flash_token      text,
    token_desc       text,
    expire_at        timestamptz,
    status           integer   NOT NULL,
    status_for_token integer   NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_user_flashtoken_expire ON user_flashtoken (expire_at);

CREATE TABLE IF NOT EXISTS user_gitlab_info
(
    user_id                       integer PRIMARY KEY,
    git_id                        integer NOT NULL,
    gitlab_impersonation_token_id integer NOT NULL,
    gitlab_impersonation_token    text    NOT NULL
);

CREATE TABLE IF NOT EXISTS user_third_party_info
(
    user_id        integer NOT NULL,
    platform_id    integer NOT NULL,
    open_id        text    NOT NULL,
    access_token   text    NOT NULL,
    nick_name      text    NOT NULL,
    avatar_img_url text    NOT NULL,
    PRIMARY KEY (user_id, platform_id)
);

CREATE TABLE IF NOT EXISTS mfa_channels
(
    id           serial PRIMARY KEY,
    channel_name text NOT NULL
);

CREATE TABLE IF NOT EXISTS mfa_records
(
    id             serial PRIMARY KEY,
    mfa_code       text    NOT NULL,
    mfa_target     text    NOT NULL,
    mfa_channel_id integer NOT NULL,
    mfa_type       integer NOT NULL,
    expire_at      timestamptz NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_mfa_records_target ON mfa_records (mfa_target, mfa_type);

CREATE TABLE IF NOT EXISTS third_party_platforms
(
    id            serial PRIMARY KEY,
    platform_name text NOT NULL
);

CREATE TABLE IF NOT EXISTS user_deleted
(
    id               serial PRIMARY KEY,
    name             text,
    password_salt    bytea,
    password_hash    bytea,
    privilege        integer,
    nickname         text,
    telephone_number text,
    email            text,
    is_male          boolean,
    create_at        timestamptz,
    delete_at        timestamptz
);

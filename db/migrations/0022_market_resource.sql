-- 0022 资源市场（Beta）
--
-- 背景
--   工作台新增「资源市场」：用户上传元件库/标准库文件包（.mdb/.db/.zip），
--   登录用户均可浏览与下载。上传沿用分片写入临时文件的方式，避免大文件一次性进内存。
--
-- 约定
--   category:     component 元件库 / standard 标准库
--   status:       1 正常 / 0 已删除（软删，下载与列表都只看 1）
--   上传会话 status: 0 上传中 / 1 已完成（保留用于幂等 complete）
--   文件落盘：<Market.StoreDir>/<resource_id>/<file_name>

CREATE TABLE IF NOT EXISTS market_resource
(
    id              bigserial PRIMARY KEY,
    owner_user_id   integer NOT NULL REFERENCES "user" (id) ON DELETE CASCADE,
    owner_team_id   integer,
    name            text     NOT NULL,
    version         text     NOT NULL DEFAULT '',
    description     text     NOT NULL DEFAULT '',
    category        text     NOT NULL,
    file_name       text     NOT NULL,
    file_size       bigint   NOT NULL DEFAULT 0,
    sha256          text     NOT NULL DEFAULT '',
    download_count  bigint   NOT NULL DEFAULT 0,
    status          smallint NOT NULL DEFAULT 1,
    created_at      timestamptz NOT NULL DEFAULT now(),
    updated_at      timestamptz NOT NULL DEFAULT now(),
    CONSTRAINT market_resource_category_check CHECK (category IN ('component', 'standard'))
);

CREATE INDEX IF NOT EXISTS idx_market_resource_status_category
    ON market_resource (status, category, id DESC);

CREATE INDEX IF NOT EXISTS idx_market_resource_owner
    ON market_resource (owner_user_id, id DESC);

CREATE TABLE IF NOT EXISTS market_upload_session
(
    id             text PRIMARY KEY,
    user_id        integer NOT NULL REFERENCES "user" (id) ON DELETE CASCADE,
    name           text     NOT NULL,
    version        text     NOT NULL DEFAULT '',
    description    text     NOT NULL DEFAULT '',
    category       text     NOT NULL,
    file_name      text     NOT NULL,
    file_size      bigint   NOT NULL,
    temp_name      text     NOT NULL,
    uploaded_bytes bigint   NOT NULL DEFAULT 0,
    status         smallint NOT NULL DEFAULT 0,
    resource_id    bigint REFERENCES market_resource (id) ON DELETE SET NULL,
    created_at     timestamptz NOT NULL DEFAULT now(),
    updated_at     timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_market_upload_session_user
    ON market_upload_session (user_id, status, updated_at DESC);

COMMENT ON TABLE market_resource IS '资源市场：元件库/标准库文件包（Beta）';
COMMENT ON COLUMN market_resource.category IS 'component 元件库 / standard 标准库';
COMMENT ON COLUMN market_resource.status IS '1 正常 / 0 已删除';
COMMENT ON TABLE market_upload_session IS '资源市场分片上传会话';
COMMENT ON COLUMN market_upload_session.uploaded_bytes IS '临时文件已写入字节数，也是下一次分片的 offset';

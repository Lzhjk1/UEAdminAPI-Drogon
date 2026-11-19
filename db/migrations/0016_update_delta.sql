-- 0016 增量更新包（文件级差量）
--
-- 背景
--   在线更新只支持全量安装包（约 542 MB），小版本更新也要整包下载。
--   按文件级差量下发后，日常更新只含新增/变更文件 + 删除清单，目标 ≤ 全量 20%。
--
-- 约定
--   from_version 必须与客户端当前版本精确匹配才下发，跨版本一律回落全量包；
--   manifest 由打包侧生成（files/deletes/forceGroups/baseFiles），客户端做基线预检，
--   安装器按清单落文件，服务端只负责原样保存与下发。
--   status: 0 草稿 / 1 已发布 / 2 已下线

CREATE TABLE IF NOT EXISTS update_delta
(
    id           serial PRIMARY KEY,
    from_version text        NOT NULL,
    to_version   text        NOT NULL,
    build_no     bigint      NOT NULL DEFAULT 0,
    file_name    text        NOT NULL,
    file_path    text        NOT NULL DEFAULT '',
    size_bytes   bigint      NOT NULL DEFAULT 0,
    sha256       text        NOT NULL DEFAULT '',
    manifest     jsonb       NOT NULL DEFAULT '{}'::jsonb,
    status       smallint    NOT NULL DEFAULT 0,
    created_at   timestamptz NOT NULL DEFAULT now(),
    published_at timestamptz,
    UNIQUE (from_version, to_version)
);

CREATE INDEX IF NOT EXISTS idx_update_delta_to_version
    ON update_delta (to_version, status);

COMMENT ON TABLE update_delta IS '增量更新包：同一基线-目标版本只保留一条记录';
COMMENT ON COLUMN update_delta.manifest IS '差量清单：files / deletes / forceGroups / baseFiles 等';

-- 分片上传会话复用：kind 区分全量 / 差量，差量额外记录基线与清单
ALTER TABLE installer_upload_session
    ADD COLUMN IF NOT EXISTS kind text NOT NULL DEFAULT 'full';
ALTER TABLE installer_upload_session
    ADD COLUMN IF NOT EXISTS from_version text;
ALTER TABLE installer_upload_session
    ADD COLUMN IF NOT EXISTS manifest jsonb;
ALTER TABLE installer_upload_session
    ADD COLUMN IF NOT EXISTS delta_id integer REFERENCES update_delta (id) ON DELETE SET NULL;

COMMENT ON COLUMN installer_upload_session.kind IS 'full=全量安装包 / delta=增量更新包';
COMMENT ON COLUMN installer_upload_session.from_version IS '差值包基线版本，kind=delta 时必填';
COMMENT ON COLUMN installer_upload_session.manifest IS '差值包清单，kind=delta 时必填';

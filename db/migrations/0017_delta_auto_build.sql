-- 0017 差量包自动生成任务
--
-- 背景
--   差量包不再由人工上传：全量包上传完成后，服务端解包新旧两个安装器，
--   比对文件树并自动编译差量 NSIS 安装器。生成过程耗时较长，用任务表记录状态，
--   上传接口立即返回，管理端轮询任务结果。
--
-- 状态: 0 排队 / 1 生成中 / 2 成功 / 3 失败 / 4 跳过（无基线或强制组变化）

CREATE TABLE IF NOT EXISTS update_delta_job
(
    id           serial PRIMARY KEY,
    from_version text        NOT NULL DEFAULT '',
    to_version   text        NOT NULL,
    status       smallint    NOT NULL DEFAULT 0,
    message      text        NOT NULL DEFAULT '',
    created_at   timestamptz NOT NULL DEFAULT now(),
    updated_at   timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_update_delta_job_created
    ON update_delta_job (created_at DESC);

COMMENT ON TABLE update_delta_job IS '差量包自动生成任务（上传/发布全量后触发）';

-- 差量来源：手工上传（保留兼容） / 服务端自动生成
ALTER TABLE update_delta
    ADD COLUMN IF NOT EXISTS origin text NOT NULL DEFAULT 'upload';

COMMENT ON COLUMN update_delta.origin IS 'upload=人工上传 / auto=服务端自动生成';

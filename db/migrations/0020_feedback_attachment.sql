-- 0020 意见反馈支持日志附件
--
-- 背景
--   客户端提交反馈时自动附带本机崩溃报告（.dmp + 同名 .log）与启动/引擎日志。
--   截图仍走 user_feedback_image；日志等非图片附件单独存表，文件同样落在
--   Feedback.StoreDir，库里只记原始文件名、MIME、大小与落盘文件名。
--
-- 约定
--   仅允许 .log / .txt / .dmp；单条反馈最多 8 个附件，
--   单文件 ≤ 12MB、合计 ≤ 20MB（服务端强校验）。
--   file_name 是展示/下载用的原始文件名，stored_name 是服务端生成的安全文件名。

CREATE TABLE IF NOT EXISTS user_feedback_attachment
(
    id          serial PRIMARY KEY,
    feedback_id integer NOT NULL REFERENCES user_feedback (id) ON DELETE CASCADE,
    seq         smallint NOT NULL DEFAULT 0,
    file_name   text NOT NULL,
    file_mime   text,
    file_size   bigint NOT NULL DEFAULT 0,
    stored_name text NOT NULL,
    created_at  timestamptz NOT NULL DEFAULT now(),
    UNIQUE (feedback_id, seq)
);

CREATE INDEX IF NOT EXISTS idx_user_feedback_attachment_feedback
    ON user_feedback_attachment (feedback_id, seq);

COMMENT ON TABLE user_feedback_attachment IS '意见反馈日志附件（崩溃报告/启动与引擎日志）';
COMMENT ON COLUMN user_feedback_attachment.file_name IS '原始文件名，展示与下载用';
COMMENT ON COLUMN user_feedback_attachment.stored_name IS '落盘文件名，位于 Feedback.StoreDir';

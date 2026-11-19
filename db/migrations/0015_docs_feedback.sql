-- 0015 文档批注反馈
--
-- 背景
--   官网文档站支持用户划词批注/整页纠错，匿名提交，可选留联系方式；
--   厂商管理员在 /admin/docs-feedback 校对并标记处理结果。
--
-- 约定
--   status: 0 待校对 / 1 已修正 / 2 忽略
--   page_path 保存提交时的站点路径（如 /site/docs/autopdms/09-piping）
--   选中的文字原文保存，便于定位；不存截图与附件。

CREATE TABLE IF NOT EXISTS docs_feedback
(
    id            serial PRIMARY KEY,
    page_path     text      NOT NULL,
    page_title    text,
    section_title text,
    selected_text text,
    content       text      NOT NULL,
    contact       text,
    user_agent    text,
    client_ip     text,
    status        smallint  NOT NULL DEFAULT 0,
    admin_note    text,
    handled_by    integer,
    handled_at    timestamptz,
    created_at    timestamptz NOT NULL DEFAULT now(),
    CONSTRAINT docs_feedback_status_check CHECK (status IN (0, 1, 2))
);

CREATE INDEX IF NOT EXISTS idx_docs_feedback_created
    ON docs_feedback (created_at DESC);
CREATE INDEX IF NOT EXISTS idx_docs_feedback_status
    ON docs_feedback (status, created_at DESC);
CREATE INDEX IF NOT EXISTS idx_docs_feedback_page
    ON docs_feedback (page_path, created_at DESC);

COMMENT ON TABLE docs_feedback IS '官网文档批注/纠错反馈';
COMMENT ON COLUMN docs_feedback.status IS '0 待校对 / 1 已修正 / 2 忽略';
COMMENT ON COLUMN docs_feedback.page_path IS '提交时站点路径';
COMMENT ON COLUMN docs_feedback.selected_text IS '用户选中的原文，便于定位';

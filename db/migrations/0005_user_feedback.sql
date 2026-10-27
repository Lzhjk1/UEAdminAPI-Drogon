-- 0005 用户意见反馈
--
-- 背景
--   登录器「我的」里新增意见反馈入口。反馈随登录态提交，落库供厂商超级管理员
--   在登录器内查看、导出；截图单独落盘，库里只记文件名与 MIME。
--
-- 约定
--   category: 0 功能建议 / 1 问题报错 / 2 其他
--   status:   0 待处理 / 1 已处理
--   user_name 是提交时的快照：用户改名或注销后，反馈仍能看出是谁提交的。

CREATE TABLE IF NOT EXISTS user_feedback
(
    id          serial PRIMARY KEY,
    user_id     integer REFERENCES "user" (id) ON DELETE SET NULL,
    user_name   text      NOT NULL,
    category    smallint  NOT NULL DEFAULT 0,
    content     text      NOT NULL,
    contact     text,
    app_version text,
    os_info     text,
    image_name  text,
    image_mime  text,
    status      smallint  NOT NULL DEFAULT 0,
    handled_by  integer,
    handled_at  timestamptz,
    created_at  timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_user_feedback_created ON user_feedback (created_at DESC);
CREATE INDEX IF NOT EXISTS idx_user_feedback_status ON user_feedback (status, created_at DESC);

COMMENT ON TABLE user_feedback IS '登录器用户意见反馈';
COMMENT ON COLUMN user_feedback.image_name IS '截图文件名，实际文件在 Feedback.StoreDir 下';

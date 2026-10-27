-- 0008 意见反馈支持多张截图（最多 5 张）
--
-- 背景
--   客户端反馈窗口要展示缩略图并允许一次附最多 5 张截图。
--   原 user_feedback.image_name/image_mime 只能存一张，改为子表；
--   旧数据回填到 seq=0，旧列保留不删，避免影响历史查询/回滚。
--
-- 幂等
--   建表/索引 IF NOT EXISTS；回填用 ON CONFLICT DO NOTHING。

CREATE TABLE IF NOT EXISTS user_feedback_image
(
    id         serial PRIMARY KEY,
    feedback_id integer NOT NULL REFERENCES user_feedback (id) ON DELETE CASCADE,
    seq        smallint NOT NULL DEFAULT 0,
    image_name text NOT NULL,
    image_mime text,
    created_at timestamptz NOT NULL DEFAULT now(),
    UNIQUE (feedback_id, seq)
);

CREATE INDEX IF NOT EXISTS idx_user_feedback_image_feedback
    ON user_feedback_image (feedback_id, seq);

-- 历史单图回填为第 0 张
INSERT INTO user_feedback_image (feedback_id, seq, image_name, image_mime)
SELECT id, 0, image_name, image_mime
FROM user_feedback
WHERE image_name IS NOT NULL AND image_name <> ''
ON CONFLICT (feedback_id, seq) DO NOTHING;

COMMENT ON TABLE user_feedback_image IS '意见反馈截图，一条反馈最多 5 张';

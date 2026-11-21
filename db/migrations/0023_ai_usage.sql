-- 0023 AI 用量记账
--
-- 背景
--   内置 AI 代理（/api/ai/chat）此前只转发模型响应，服务端不记录 token 消耗，
--   无法按团队 / 席位核算成本。本表按「每次模型调用一行」记录用量，
--   上游返回的 usage 字段原样落库，不做价格换算（单价会变，token 不会）。
--
-- 约定
--   team_id：记账时用户当前所在团队；无团队为 NULL。团队删除后历史用量保留。
--   cache_*_tokens：优先取 DeepSeek 的 prompt_cache_hit_tokens /
--                   prompt_cache_miss_tokens；其它 OpenAI 兼容厂商用
--                   prompt_tokens_details.cached_tokens 折算，缺失时为 0。
--   查询按 created_at 的东八区自然月聚合。

CREATE TABLE IF NOT EXISTS ai_usage_log
(
    id                bigserial PRIMARY KEY,
    user_id           integer NOT NULL REFERENCES "user" (id) ON DELETE CASCADE,
    team_id           integer REFERENCES team (id) ON DELETE SET NULL,
    model             text    NOT NULL DEFAULT '',
    prompt_tokens     integer NOT NULL DEFAULT 0,
    completion_tokens integer NOT NULL DEFAULT 0,
    total_tokens      integer NOT NULL DEFAULT 0,
    cache_hit_tokens  integer NOT NULL DEFAULT 0,
    cache_miss_tokens integer NOT NULL DEFAULT 0,
    created_at        timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_ai_usage_log_team_time
    ON ai_usage_log (team_id, created_at DESC);

CREATE INDEX IF NOT EXISTS idx_ai_usage_log_user_time
    ON ai_usage_log (user_id, created_at DESC);

COMMENT ON TABLE ai_usage_log IS '内置 AI 代理按次调用用量（团队/席位成本核算）';
COMMENT ON COLUMN ai_usage_log.team_id IS '记账时用户所在团队；无团队为 NULL';
COMMENT ON COLUMN ai_usage_log.model IS '上游返回的模型名，缺失时取服务端配置';
COMMENT ON COLUMN ai_usage_log.cache_hit_tokens IS '提示词缓存命中 token（DeepSeek/OpenAI cached_tokens）';
COMMENT ON COLUMN ai_usage_log.cache_miss_tokens IS '提示词缓存未命中 token';

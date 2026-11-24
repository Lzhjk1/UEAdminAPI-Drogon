-- 0028 AI 服务管理：模型档位 / 多上游 / 企业自带 Key / 并发与 TPM
--
-- 设计
--   ai_setting：单行 JSON，保存标准/旗舰模型、并发上限、TPM、告警阈值。
--   ai_upstream：平台级上游 Key（Token Plan 套餐 / TokenHub 按量），Key 只存密文。
--   team_ai_provider：企业级 BYOK，一个团队一行；企业用自己的腾讯云 Key。
--   ai_usage_log 增加 upstream/tier 维度，便于按上游与企业对账。
--
-- 安全
--   api_key_enc 由服务端 AI.Secret / UE_AI_SECRET 做 AES-256-GCM 加密，
--   接口只返回掩码（key_hint），密文永不下发客户端。

CREATE TABLE IF NOT EXISTS ai_setting
(
    id         integer PRIMARY KEY,
    data       jsonb NOT NULL DEFAULT '{}'::jsonb,
    updated_at timestamptz NOT NULL DEFAULT now(),
    CONSTRAINT ai_setting_singleton CHECK (id = 1)
);

INSERT INTO ai_setting (id, data) VALUES (1, '{}'::jsonb)
ON CONFLICT (id) DO NOTHING;

CREATE TABLE IF NOT EXISTS ai_upstream
(
    id              bigserial PRIMARY KEY,
    name            text NOT NULL,
    kind            text NOT NULL DEFAULT 'plan',
    base_url        text NOT NULL,
    api_key_enc     text NOT NULL,
    key_hint        text NOT NULL DEFAULT '',
    model_standard  text NOT NULL DEFAULT '',
    model_flagship  text NOT NULL DEFAULT '',
    priority        integer NOT NULL DEFAULT 100,
    enabled         boolean NOT NULL DEFAULT true,
    monthly_budget  bigint NOT NULL DEFAULT 0,
    created_at      timestamptz NOT NULL DEFAULT now(),
    updated_at      timestamptz NOT NULL DEFAULT now(),
    CONSTRAINT ai_upstream_kind CHECK (kind IN ('plan', 'payg'))
);

CREATE INDEX IF NOT EXISTS idx_ai_upstream_priority
    ON ai_upstream (enabled, priority, id);

CREATE TABLE IF NOT EXISTS team_ai_provider
(
    team_id         integer PRIMARY KEY REFERENCES team (id) ON DELETE CASCADE,
    name            text NOT NULL DEFAULT '',
    base_url        text NOT NULL,
    api_key_enc     text NOT NULL,
    key_hint        text NOT NULL DEFAULT '',
    model_standard  text NOT NULL DEFAULT '',
    model_flagship  text NOT NULL DEFAULT '',
    enabled         boolean NOT NULL DEFAULT true,
    created_at      timestamptz NOT NULL DEFAULT now(),
    updated_at      timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_team_ai_provider_enabled
    ON team_ai_provider (enabled);

ALTER TABLE ai_usage_log ADD COLUMN IF NOT EXISTS upstream_id bigint;
ALTER TABLE ai_usage_log ADD COLUMN IF NOT EXISTS upstream_name text;
ALTER TABLE ai_usage_log ADD COLUMN IF NOT EXISTS provider_kind text;
ALTER TABLE ai_usage_log ADD COLUMN IF NOT EXISTS model_tier text;

COMMENT ON TABLE ai_setting IS 'AI 服务管理单行设置（模型档位/并发/TPM/告警）';
COMMENT ON TABLE ai_upstream IS '平台级模型上游；Key 以 AES-256-GCM 密文保存';
COMMENT ON COLUMN ai_upstream.monthly_budget IS '套餐按 tokens；按量按分（0 表示不限额）';
COMMENT ON TABLE team_ai_provider IS '企业自带 Key（BYOK）：一个团队一条，优先于平台上游';
COMMENT ON COLUMN ai_usage_log.upstream_name IS '实际命中的上游名称（含企业自带）';
COMMENT ON COLUMN ai_usage_log.model_tier IS 'standard / flagship';

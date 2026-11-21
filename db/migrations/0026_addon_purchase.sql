-- 0026 积分包 / 云工程空间在线购买
--
-- payment_order 记录 SKU 与归属团队；结算时不再只加余额，
-- 而是按 product_code 发放 team_addon（积分包 30 天 / 云空间 365 天）。

ALTER TABLE payment_order ADD COLUMN IF NOT EXISTS product_code text;
ALTER TABLE payment_order ADD COLUMN IF NOT EXISTS team_id integer REFERENCES team (id);
ALTER TABLE payment_order ADD COLUMN IF NOT EXISTS product_meta jsonb;

CREATE INDEX IF NOT EXISTS idx_payment_order_product
    ON payment_order (product_code) WHERE product_code IS NOT NULL;

CREATE TABLE IF NOT EXISTS team_addon
(
    id         bigserial PRIMARY KEY,
    team_id    integer NOT NULL REFERENCES team (id),
    kind       text    NOT NULL CHECK (kind IN ('credits', 'storage')),
    amount     integer NOT NULL CHECK (amount > 0),
    order_no   text    NOT NULL UNIQUE,
    start_at   timestamptz NOT NULL DEFAULT now(),
    end_at     timestamptz NOT NULL,
    status     smallint NOT NULL DEFAULT 1,
    created_at timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_team_addon_team_kind
    ON team_addon (team_id, kind, end_at DESC);

COMMENT ON TABLE team_addon IS '团队加购：积分包（credits，30 天）与云工程空间（storage，GB，365 天）';
COMMENT ON COLUMN team_addon.amount IS 'credits=积分数量；storage=GB';
COMMENT ON COLUMN team_addon.order_no IS '来源支付订单号，唯一，保证支付回调幂等发放';

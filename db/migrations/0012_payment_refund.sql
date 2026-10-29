-- 余额充值退款申请：用户申请 -> 超级管理员审核 -> 渠道退款 -> 扣减余额。
-- status: 0 待审核 1 退款中(已扣余额，渠道处理中) 2 已退款 3 已驳回 4 退款失败(余额已冲正)
CREATE TABLE IF NOT EXISTS payment_refund
(
    id                 bigserial PRIMARY KEY,
    refund_no          text   NOT NULL UNIQUE,
    order_no           text   NOT NULL REFERENCES payment_order (order_no),
    user_id            integer NOT NULL REFERENCES "user" (id),
    amount_cents       bigint NOT NULL,
    reason             text,
    status             smallint NOT NULL DEFAULT 0,
    review_by          integer REFERENCES "user" (id),
    review_remark      text,
    reviewed_at        timestamptz,
    provider_refund_no text,
    provider_state     text,
    error_msg          text,
    refunded_at        timestamptz,
    created_at         timestamptz NOT NULL DEFAULT now(),
    updated_at         timestamptz NOT NULL DEFAULT now()
);

-- 同一订单同时只允许一条有效申请（待审核/退款中/已退款）；驳回或失败后可重新申请
CREATE UNIQUE INDEX IF NOT EXISTS uq_payment_refund_active
    ON payment_refund (order_no) WHERE status IN (0, 1, 2);

CREATE INDEX IF NOT EXISTS idx_payment_refund_status ON payment_refund (status, created_at DESC);
CREATE INDEX IF NOT EXISTS idx_payment_refund_user ON payment_refund (user_id, created_at DESC);

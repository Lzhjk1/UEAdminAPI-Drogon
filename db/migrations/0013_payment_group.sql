-- 支付组：一次充值同时生成微信/支付宝两笔订单，任一方支付成功后关闭另一笔
ALTER TABLE payment_order ADD COLUMN IF NOT EXISTS pay_group text;
CREATE INDEX IF NOT EXISTS idx_payment_order_group ON payment_order (pay_group);

-- 幂等键按用户隔离：原全局唯一会让不同用户同一秒同金额互相冲突
ALTER TABLE payment_order DROP CONSTRAINT IF EXISTS payment_order_idem_key_key;
CREATE UNIQUE INDEX IF NOT EXISTS uq_payment_order_user_idem
    ON payment_order (user_id, idem_key) WHERE idem_key IS NOT NULL;

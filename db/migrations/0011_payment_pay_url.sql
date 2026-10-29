-- 支付宝电脑网站支付：预下单返回的是收银台 URL，不是二维码串。
-- qr_content 继续保存二维码内容（微信 Native / 支付宝订单码），pay_url 保存网页收银台地址。
ALTER TABLE payment_order ADD COLUMN IF NOT EXISTS pay_url text;

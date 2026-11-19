-- 0017 新用户试用
--
-- team_subscription 增加 is_trial 标记：
--   true  = 新注册用户自动开通的试用期，到期后需联系销售开通或续费；
--   false = 厂商后台设置的正式付费期，存量记录全部按 false 回填。
--
-- 试用期同样占用 start_date / end_date，因此现有「取 end_date 最大且 status=1」
-- 的订阅判定逻辑无需调整；是否试用只影响到期提示语与后台展示。

ALTER TABLE team_subscription
    ADD COLUMN IF NOT EXISTS is_trial boolean NOT NULL DEFAULT false;

COMMENT ON COLUMN team_subscription.is_trial IS
    'true=注册赠送试用期，false=正式订阅/历史付费记录';

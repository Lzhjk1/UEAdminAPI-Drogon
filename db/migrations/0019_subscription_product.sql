-- 0019 订阅产品化（PDMS / UEAICAD）
--
-- team_subscription 增加 product：
--   pdms    = 传统 PDMS 套件（网络授权，按团队一价，不限节点）；
--   ueaicad = AI 工作台加购，权限上隐含 pdms。
--
-- 存量记录全部回填 ueaicad：现有客户继续保有原 AI 套餐，
-- 并按新规则自动获得 PDMS 权限。PDMS-only 记录由销售新建。

ALTER TABLE team_subscription
    ADD COLUMN IF NOT EXISTS product text NOT NULL DEFAULT 'ueaicad';

UPDATE team_subscription SET product = 'ueaicad'
    WHERE product IS NULL OR product = '';

ALTER TABLE team_subscription DROP CONSTRAINT IF EXISTS ck_team_subscription_product;
ALTER TABLE team_subscription ADD CONSTRAINT ck_team_subscription_product
    CHECK (product IN ('pdms', 'ueaicad'));

CREATE INDEX IF NOT EXISTS idx_team_subscription_team_product
    ON team_subscription (team_id, product, end_date DESC);

COMMENT ON COLUMN team_subscription.product IS
    'pdms=传统套件(网络授权, 不限节点); ueaicad=AI 工作台(隐含 pdms)';

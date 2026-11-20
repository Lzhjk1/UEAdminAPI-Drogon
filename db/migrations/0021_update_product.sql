-- 0021 更新通道产品维度（pdms / ueaicad）
--
-- 背景
--   UE AI CAD 作为独立产品拥有自己的安装包与版本线，与 AutoPDMS 共用更新服务。
--   四张更新表增加 product 字段，唯一约束改为按产品隔离；存量数据默认 pdms，
--   老客户端不带 product 参数时行为不变。

ALTER TABLE installer_version
    ADD COLUMN IF NOT EXISTS product text NOT NULL DEFAULT 'pdms';

ALTER TABLE installer_upload_session
    ADD COLUMN IF NOT EXISTS product text NOT NULL DEFAULT 'pdms';

ALTER TABLE update_delta
    ADD COLUMN IF NOT EXISTS product text NOT NULL DEFAULT 'pdms';

ALTER TABLE update_delta_job
    ADD COLUMN IF NOT EXISTS product text NOT NULL DEFAULT 'pdms';

-- 唯一约束按产品拆分：同一版本号可同时存在于两个产品
ALTER TABLE installer_version DROP CONSTRAINT IF EXISTS installer_version_version_key;
CREATE UNIQUE INDEX IF NOT EXISTS uq_installer_version_product_version
    ON installer_version (product, version);

ALTER TABLE update_delta DROP CONSTRAINT IF EXISTS update_delta_from_version_to_version_key;
CREATE UNIQUE INDEX IF NOT EXISTS uq_update_delta_product_versions
    ON update_delta (product, from_version, to_version);

-- 客户端 check 与列表查询按产品过滤
CREATE INDEX IF NOT EXISTS idx_installer_version_product_status
    ON installer_version (product, status, id DESC);

CREATE INDEX IF NOT EXISTS idx_update_delta_job_product_created
    ON update_delta_job (product, created_at DESC);

COMMENT ON COLUMN installer_version.product IS '产品线：pdms / ueaicad';
COMMENT ON COLUMN installer_upload_session.product IS '产品线：pdms / ueaicad';
COMMENT ON COLUMN update_delta.product IS '产品线：pdms / ueaicad';
COMMENT ON COLUMN update_delta_job.product IS '产品线：pdms / ueaicad';

-- 0027 资源市场：多类型标签、官方库包与关联标准
--
-- 背景
--   资源从“元件库 / 标准库”单选改为“可同时属于多个类型”：
--     component 元件库 / spec 等级库 / design 设计库 / standard 标准库
--   旧 category 列保留并与 kinds 第一个值同步，兼容旧客户端与旧查询。
--   official + standard_code 由管理员维护：官方库包可关联到内置工程标准的编号。
--   kinds 在导入工程标准时由客户端探测库表结构后回填（Spec/CATA/DESI）。

ALTER TABLE market_resource
    ADD COLUMN IF NOT EXISTS kinds text[] NOT NULL DEFAULT '{}';
ALTER TABLE market_resource
    ADD COLUMN IF NOT EXISTS official boolean NOT NULL DEFAULT false;
ALTER TABLE market_resource
    ADD COLUMN IF NOT EXISTS standard_code text NOT NULL DEFAULT '';
ALTER TABLE market_resource
    DROP CONSTRAINT IF EXISTS market_resource_category_check;

UPDATE market_resource
SET kinds = ARRAY[category]
WHERE kinds = '{}' AND category IN ('component', 'standard');

ALTER TABLE market_upload_session
    ADD COLUMN IF NOT EXISTS kinds text[] NOT NULL DEFAULT '{}';

UPDATE market_upload_session
SET kinds = ARRAY[category]
WHERE kinds = '{}' AND category IN ('component', 'standard');

CREATE INDEX IF NOT EXISTS idx_market_resource_official_code
    ON market_resource (official, standard_code)
    WHERE status = 1;

COMMENT ON COLUMN market_resource.kinds IS '资源包含的库类型：component/spec/design/standard，可多值';
COMMENT ON COLUMN market_resource.category IS '兼容旧接口：取 kinds 第一个值';
COMMENT ON COLUMN market_resource.official IS '官方库包（仅管理员可设置）';
COMMENT ON COLUMN market_resource.standard_code IS '关联内置标准编号，如 GB/T 17116、SH/T 3073';
COMMENT ON COLUMN market_upload_session.kinds IS '上传时声明的库类型，可多值';

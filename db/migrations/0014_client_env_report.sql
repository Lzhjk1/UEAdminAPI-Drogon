-- 0014 客户端环境信息上报
--
-- 背景
--   客户端在登录成功后自动采集本机环境（CAD 版本、硬件、操作系统、依赖运行时等）
--   并上报，供厂商超级管理员在登录器内查看、导出 Markdown，用于兼容性诊断与现场排障。
--
-- 约定
--   (user_id, device_key) 唯一：同一用户同一台电脑重复上报只覆盖更新，保留 first_seen；
--   user_name 是上报时的快照，用户改名或注销后仍能看出是谁上报的；
--   payload 存完整环境 JSON，宽表只放列表/排序要用的少量列，后续加字段不用改表；
--   device_key 是客户端本地计算的加盐 SHA256（MachineGuid），不含硬件序列号明文。

CREATE TABLE IF NOT EXISTS client_env_report
(
    id              serial PRIMARY KEY,
    user_id         integer REFERENCES "user" (id) ON DELETE SET NULL,
    user_name       text      NOT NULL,
    device_key      text      NOT NULL,
    product_version text      NOT NULL DEFAULT '',
    payload         jsonb     NOT NULL DEFAULT '{}'::jsonb,
    first_seen      timestamptz NOT NULL DEFAULT now(),
    last_seen       timestamptz NOT NULL DEFAULT now(),
    UNIQUE (user_id, device_key)
);

CREATE INDEX IF NOT EXISTS idx_client_env_report_last_seen ON client_env_report (last_seen DESC);

COMMENT ON TABLE client_env_report IS '客户端登录后上报的本机环境信息（同一用户同一设备只保留最新一条）';
COMMENT ON COLUMN client_env_report.device_key IS '客户端加盐 SHA256（MachineGuid），仅用于区分设备';
COMMENT ON COLUMN client_env_report.product_version IS '上报时的客户端版本号快照';
COMMENT ON COLUMN client_env_report.payload IS '完整环境信息 JSON（cad/os/cpu/memory/disk/gpu/screen/boot/machine/deps/modules）';

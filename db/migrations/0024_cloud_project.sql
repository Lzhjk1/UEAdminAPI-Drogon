-- 0024 云工程文件元数据
--
-- 背景
--   云工程按「客户端直连存储服务（COS 预签名 URL）」设计：文件本体不经过业务服务器，
--   服务端只保存元数据、配额并签发短期上传/下载链接。
--
-- 约定
--   object_key：{KeyPrefix}/{team_id}/{project_name}/{relative_path}，全团队内唯一；
--               同一工程同一相对路径重复上传视为覆盖（ON CONFLICT 更新）。
--   sha256：客户端上传前计算，可为空（老客户端）；服务端不换算价格。
--   file_size：Driver=local 时以落盘实际大小为准；COS 以客户端声明值记录。
--   配额按套餐：plus/trial 10GB、pro/standard/legacy 或无订阅记录 50GB、max/厂商 200GB；
--   订阅到期只读（可下载，不能上传/删除）。

CREATE TABLE IF NOT EXISTS cloud_project_file
(
    id            bigserial PRIMARY KEY,
    team_id       integer NOT NULL REFERENCES team (id) ON DELETE CASCADE,
    uploaded_by   integer REFERENCES "user" (id) ON DELETE SET NULL,
    project_name  text    NOT NULL,
    relative_path text    NOT NULL,
    object_key    text    NOT NULL,
    file_size     bigint  NOT NULL DEFAULT 0,
    sha256        text    NOT NULL DEFAULT '',
    created_at    timestamptz NOT NULL DEFAULT now(),
    updated_at    timestamptz NOT NULL DEFAULT now(),
    CONSTRAINT cloud_project_file_key_unique UNIQUE (team_id, object_key)
);

CREATE INDEX IF NOT EXISTS idx_cloud_project_file_team_project
    ON cloud_project_file (team_id, project_name);

CREATE INDEX IF NOT EXISTS idx_cloud_project_file_team_time
    ON cloud_project_file (team_id, updated_at DESC);

COMMENT ON TABLE cloud_project_file IS '云工程文件元数据（文件本体在 COS/本地存储）';
COMMENT ON COLUMN cloud_project_file.object_key IS '存储对象键：{KeyPrefix}/{team_id}/{project_name}/{relative_path}';
COMMENT ON COLUMN cloud_project_file.file_size IS '字节数；local 驱动以落盘大小为准';

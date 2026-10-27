-- 0007 安装包分片上传会话（断点续传）
--
-- 背景
--   安装包动辄几百 MB，原 multipart 一次性上传中断后只能从 0 重来。
--   改为按分片写入临时文件；客户端重开、换网络后可凭文件 sha256 找回会话与已上传偏移。
--
-- 约定
--   status:     0 上传中 / 1 已完成（保留 7 天用于重复 complete 的幂等返回）
--   uploaded_bytes 是已经写到临时文件的字节数（也是下一次 chunk 的 offset）
--   temp_name 只存文件名，实际路径由 Installer.UploadTempDir 拼出

CREATE TABLE IF NOT EXISTS installer_upload_session
(
    id             text PRIMARY KEY,
    user_id        integer NOT NULL REFERENCES "user" (id) ON DELETE CASCADE,
    version        text      NOT NULL,
    build_no       bigint,
    notes          text,
    min_version    text,
    file_name      text      NOT NULL,
    file_size      bigint    NOT NULL,
    file_sha256    text      NOT NULL,
    temp_name      text      NOT NULL,
    uploaded_bytes bigint    NOT NULL DEFAULT 0,
    status         smallint  NOT NULL DEFAULT 0,
    installer_id   integer REFERENCES installer_version (id) ON DELETE SET NULL,
    created_at     timestamptz NOT NULL DEFAULT now(),
    updated_at     timestamptz NOT NULL DEFAULT now()
);

-- 同一个管理员、同一个文件只保留一个未完成会话（init 时按这三个字段找回）
CREATE UNIQUE INDEX IF NOT EXISTS uq_installer_upload_session_pending
    ON installer_upload_session (user_id, file_sha256, file_size)
    WHERE status = 0;

CREATE INDEX IF NOT EXISTS idx_installer_upload_session_updated
    ON installer_upload_session (updated_at);

COMMENT ON TABLE installer_upload_session IS '安装包分片上传会话，支持断点续传';
COMMENT ON COLUMN installer_upload_session.uploaded_bytes IS '临时文件已写入字节数，也是下一次分片的 offset';

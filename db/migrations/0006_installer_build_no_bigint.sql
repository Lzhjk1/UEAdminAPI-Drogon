-- 0006 安装包构建号扩为 64 位
--
-- 背景
--   打包文件名里的构建号是 yyyyMMddHHmm（12 位），原来的 int32 放不下，
--   客户端无法自动预填。改为 bigint 后，build- / build_ 后面的完整数字都能保存与展示。
--
-- 幂等
--   bigint -> bigint 的 ALTER 是 no-op，重复执行不会报错。

ALTER TABLE installer_version ALTER COLUMN build_no TYPE bigint;

COMMENT ON COLUMN installer_version.build_no IS '构建号：文件名 build- / build_ 后的数字，仅展示、不参与更新版本比较';

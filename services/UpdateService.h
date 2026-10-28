#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include "utils/HttpResult.h"

namespace UEAdminAPI {
namespace Services {

// 安装包在线更新：
//   客户端 --/api/update/check--> 服务端判定（已付费 + 云端更新授权 + 有更新包）
//   上传/发布走厂商后台（multipart），文件落在 Installer.StoreDir，下载由本服务直出。
class UpdateService
{
public:
    // 检查更新（客户端的唯一入口）
    static utils::HttpResult CheckUpdate(int userId,
                                         const std::string &currentVersion,
                                         const std::string &platform);

    // 厂商：分片上传（断点续传）——初始化会话，返回 uploadId / 已上传偏移 / 分片大小
    static utils::HttpResult AdminInitUpload(int userId,
                                             const std::string &version,
                                             int64_t buildNo,
                                             const std::string &notes,
                                             const std::string &minVersion,
                                             const std::string &fileName,
                                             int64_t fileSize,
                                             const std::string &fileSha256);

    // 厂商：写一个分片（offset 必须等于服务端当前已上传字节数，或返回当前值让客户端重同步）
    static utils::HttpResult AdminUploadChunk(int userId,
                                              const std::string &uploadId,
                                              int64_t offset,
                                              std::string_view data);

    // 厂商：完成上传——校验 sha256、移动文件、落 installer_version 草稿
    static utils::HttpResult AdminCompleteUpload(int userId, const std::string &uploadId);

    // 厂商：上传安装包（草稿态，旧的一体式 multipart 接口保留）
    static utils::HttpResult AdminUploadInstaller(const std::string &version,
                                                  const std::string &notes,
                                                  int64_t buildNo,
                                                  const std::string &minVersion,
                                                  const std::string &fileName,
                                                  std::string_view content);

    // 厂商：编辑安装包的版本信息（版本号 / 构建号 / 最低版本 / 备注）
    // 修改 version 只改库里记录，磁盘文件仍留在原版本目录下（下载按库里的 file_path 取）。
    static utils::HttpResult AdminUpdateInstaller(int installerId,
                                                  const std::string &version,
                                                  int64_t buildNo,
                                                  const std::string &notes,
                                                  const std::string &minVersion);

    // 厂商：发布（同一时刻只保留一个已发布版本）
    static utils::HttpResult AdminPublishInstaller(int installerId);

    // 厂商：下线（保留记录，不再对外提供更新）
    static utils::HttpResult AdminOfflineInstaller(int installerId);

    // 厂商：删除安装包（仅限已下线；删库记录 + 磁盘文件/目录，不可恢复）
    static utils::HttpResult AdminDeleteInstaller(int installerId);

    // 厂商：安装包列表
    static utils::HttpResult AdminListInstallers();

    // 下载：校验同一套授权后返回磁盘路径（不存在/未授权返回 false 并填充 error）
    static bool ResolveDownload(int userId, const std::string &version,
                                std::string &outPath, std::string &outFileName,
                                utils::HttpResult &errorResult);
};

} // namespace Services
} // namespace UEAdminAPI

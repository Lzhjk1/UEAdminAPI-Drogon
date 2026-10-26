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

    // 厂商：上传安装包（草稿态）
    static utils::HttpResult AdminUploadInstaller(const std::string &version,
                                                  const std::string &notes,
                                                  int buildNo,
                                                  const std::string &fileName,
                                                  std::string_view content);

    // 厂商：发布（同一时刻只保留一个已发布版本）
    static utils::HttpResult AdminPublishInstaller(int installerId);

    // 厂商：安装包列表
    static utils::HttpResult AdminListInstallers();

    // 下载：校验同一套授权后返回磁盘路径（不存在/未授权返回 false 并填充 error）
    static bool ResolveDownload(int userId, const std::string &version,
                                std::string &outPath, std::string &outFileName,
                                utils::HttpResult &errorResult);
};

} // namespace Services
} // namespace UEAdminAPI

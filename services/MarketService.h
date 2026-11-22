#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "utils/HttpResult.h"

namespace UEAdminAPI {
namespace Services {

// 资源市场（Beta）：登录用户上传/下载元件库、等级库、设计库与标准库文件包。
class MarketService
{
public:
    static utils::HttpResult ListResources(int userId,
                                           const std::string &category,
                                           const std::string &keyword,
                                           int page,
                                           int pageSize);

    static utils::HttpResult InitUpload(int userId,
                                        const std::string &name,
                                        const std::string &version,
                                        const std::string &description,
                                        const std::string &category,
                                        const std::vector<std::string> &kinds,
                                        const std::string &fileName,
                                        int64_t fileSize);

    static utils::HttpResult UploadChunk(int userId,
                                         const std::string &uploadId,
                                         int64_t offset,
                                         std::string_view data);

    static utils::HttpResult CompleteUpload(int userId, const std::string &uploadId);

    static utils::HttpResult DeleteResource(int userId, int64_t resourceId);

    // 导入工程标准时由客户端探测库表结构后回填类型标签
    static utils::HttpResult UpdateKinds(int userId,
                                         int64_t resourceId,
                                         const std::vector<std::string> &kinds);

    // 管理员设置官方库包与关联标准编号
    static utils::HttpResult UpdateMeta(int userId,
                                        int64_t resourceId,
                                        bool hasOfficial,
                                        bool official,
                                        bool hasStandardCode,
                                        const std::string &standardCode);

    static bool ResolveDownload(int userId,
                                int64_t resourceId,
                                std::string &path,
                                std::string &fileName,
                                utils::HttpResult &error);
};

} // namespace Services
} // namespace UEAdminAPI

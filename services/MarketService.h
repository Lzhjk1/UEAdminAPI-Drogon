#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "utils/HttpResult.h"

namespace UEAdminAPI {
namespace Services {

// 资源市场（Beta）：登录用户上传/下载元件库与标准库文件包。
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
                                        const std::string &fileName,
                                        int64_t fileSize);

    static utils::HttpResult UploadChunk(int userId,
                                         const std::string &uploadId,
                                         int64_t offset,
                                         std::string_view data);

    static utils::HttpResult CompleteUpload(int userId, const std::string &uploadId);

    static utils::HttpResult DeleteResource(int userId, int64_t resourceId);

    static bool ResolveDownload(int userId,
                                int64_t resourceId,
                                std::string &path,
                                std::string &fileName,
                                utils::HttpResult &error);
};

} // namespace Services
} // namespace UEAdminAPI

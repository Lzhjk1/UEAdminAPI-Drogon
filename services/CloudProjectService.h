#pragma once

#include <string>

#include "utils/HttpResult.h"

namespace UEAdminAPI {
namespace Services {

struct CloudConfig
{
    bool enabled = false;
    std::string driver = "cos"; // cos | local
    int urlExpireSec = 900;
    long long maxFileSizeBytes = 512LL * 1024 * 1024;
    std::string keyPrefix = "cloud-projects";
    std::string localStoreDir = "./uploads/cloud";
    std::string localSecret;
    std::string scheme = "https";
    std::string secretId;
    std::string secretKey;
    std::string bucket;
    std::string region = "ap-shanghai";
    std::string endpoint; // 留空用 {bucket}.cos.{region}.myqcloud.com
};

// 云工程：团队私有空间，客户端凭预签名 URL 直传/直下 COS。
// 元数据与配额在本服务，文件本体不经过业务服务器（Driver=local 仅用于开发与私有部署）。
class CloudProjectService
{
public:
    static CloudConfig LoadConfig();

    static utils::HttpResult Status(int userId);
    // baseUrl 形如 http://127.0.0.1:8080，供 Driver=local 直传地址使用。
    static utils::HttpResult Prepare(int userId,
                                     const Json::Value &body,
                                     const std::string &baseUrl);
    static utils::HttpResult Complete(int userId, const Json::Value &body);
    static utils::HttpResult List(int userId, const std::string &projectName);
    static utils::HttpResult Download(int userId,
                                      long long fileId,
                                      const std::string &baseUrl);
    static utils::HttpResult Remove(int userId, long long fileId);

    // 本地驱动的直传/直下令牌与落盘路径。
    static bool VerifyLocalToken(const std::string &objectKey,
                                 const std::string &method,
                                 long long exp,
                                 const std::string &token);
    static bool LocalPathFor(const std::string &objectKey, std::string &path);
    static long long MaxFileSizeBytes();
};

} // namespace Services
} // namespace UEAdminAPI

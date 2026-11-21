#pragma once

#include <string>

namespace UEAdminAPI {
namespace utils {

// 腾讯云 COS XML API 的签名版本 v5（q-sign-algorithm=sha1）。
// 约定：签名用的路径是未经 URL 编码的原始 key；最终 URL 再对路径编码。
class CosSigner
{
public:
    // 保留 '/' 的路径编码（UTF-8 字节，%XX 大写）。
    static std::string EncodePath(const std::string &path);
    // 查询参数值编码（不保留 '/'）。
    static std::string EncodeValue(const std::string &value);

    static std::string Sha1Hex(const std::string &data);
    static std::string HmacSha1Hex(const std::string &key, const std::string &data);
    static std::string HmacSha256Hex(const std::string &key, const std::string &data);

    // Authorization 头（header 传入方式），q-sign-time 保留分号。
    static std::string Authorization(const std::string &secretId,
                                     const std::string &secretKey,
                                     const std::string &method,
                                     const std::string &rawPath,
                                     const std::string &host,
                                     long long nowUnix,
                                     long long expireSec);

    // 预签名 URL（query 传入方式），q-sign-time 的分号编码为 %3B。
    static std::string PresignUrl(const std::string &scheme,
                                  const std::string &host,
                                  const std::string &objectKey,
                                  const std::string &secretId,
                                  const std::string &secretKey,
                                  const std::string &method,
                                  long long nowUnix,
                                  long long expireSec);

private:
    static std::string SignQuery(const std::string &secretId,
                                 const std::string &secretKey,
                                 const std::string &method,
                                 const std::string &rawPath,
                                 const std::string &host,
                                 long long nowUnix,
                                 long long expireSec,
                                 bool encodeKeyTime);
};

} // namespace utils
} // namespace UEAdminAPI

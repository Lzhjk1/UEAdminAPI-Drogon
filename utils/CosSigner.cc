#include "CosSigner.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <algorithm>
#include <cctype>

namespace UEAdminAPI {
namespace utils {

namespace
{

std::string ToHex(const unsigned char *data, unsigned int len)
{
    static const char *kHex = "0123456789abcdef";
    std::string out(len * 2, '0');
    for (unsigned int i = 0; i < len; ++i)
    {
        out[i * 2] = kHex[data[i] >> 4];
        out[i * 2 + 1] = kHex[data[i] & 0x0F];
    }
    return out;
}

bool IsUnreserved(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
}

std::string Encode(const std::string &value, bool keepSlash)
{
    static const char *kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for (const unsigned char c : value)
    {
        if (IsUnreserved(c) || (keepSlash && c == '/'))
        {
            out.push_back(static_cast<char>(c));
            continue;
        }
        out.push_back('%');
        out.push_back(kHex[c >> 4]);
        out.push_back(kHex[c & 0x0F]);
    }
    return out;
}

std::string LowerMethod(const std::string &method)
{
    std::string lower = method;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

} // namespace

std::string CosSigner::EncodePath(const std::string &path)
{
    return Encode(path, true);
}

std::string CosSigner::EncodeValue(const std::string &value)
{
    return Encode(value, false);
}

std::string CosSigner::Sha1Hex(const std::string &data)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx)
    {
        return "";
    }
    EVP_DigestInit_ex(ctx, EVP_sha1(), nullptr);
    EVP_DigestUpdate(ctx, data.data(), data.size());
    EVP_DigestFinal_ex(ctx, digest, &len);
    EVP_MD_CTX_free(ctx);
    return ToHex(digest, len);
}

std::string CosSigner::HmacSha1Hex(const std::string &key, const std::string &data)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    HMAC(EVP_sha1(), key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char *>(data.data()), data.size(), digest, &len);
    return ToHex(digest, len);
}

std::string CosSigner::HmacSha256Hex(const std::string &key, const std::string &data)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char *>(data.data()), data.size(), digest, &len);
    return ToHex(digest, len);
}

std::string CosSigner::SignQuery(const std::string &secretId,
                                 const std::string &secretKey,
                                 const std::string &method,
                                 const std::string &rawPath,
                                 const std::string &host,
                                 long long nowUnix,
                                 long long expireSec,
                                 bool encodeKeyTime)
{
    const std::string keyTime = std::to_string(nowUnix - 60) + ";" + std::to_string(nowUnix + expireSec);
    const std::string signKey = HmacSha1Hex(secretKey, keyTime);

    // 预签名默认签入 host；q-url-param-list 为空。
    const std::string httpHeaders = "host=" + EncodeValue(host);
    const std::string httpString = LowerMethod(method) + "\n" + rawPath + "\n" + "" + "\n" +
                                   httpHeaders + "\n";
    const std::string stringToSign = "sha1\n" + keyTime + "\n" + Sha1Hex(httpString) + "\n";
    const std::string signature = HmacSha1Hex(signKey, stringToSign);
    const std::string keyTimeOut = encodeKeyTime ? EncodeValue(keyTime) : keyTime;

    return "q-sign-algorithm=sha1&q-ak=" + secretId +
           "&q-sign-time=" + keyTimeOut +
           "&q-key-time=" + keyTimeOut +
           "&q-header-list=host&q-url-param-list=&q-signature=" + signature;
}

std::string CosSigner::Authorization(const std::string &secretId,
                                     const std::string &secretKey,
                                     const std::string &method,
                                     const std::string &rawPath,
                                     const std::string &host,
                                     long long nowUnix,
                                     long long expireSec)
{
    return SignQuery(secretId, secretKey, method, rawPath, host, nowUnix, expireSec, false);
}

std::string CosSigner::PresignUrl(const std::string &scheme,
                                  const std::string &host,
                                  const std::string &objectKey,
                                  const std::string &secretId,
                                  const std::string &secretKey,
                                  const std::string &method,
                                  long long nowUnix,
                                  long long expireSec)
{
    std::string rawPath = objectKey.empty() || objectKey.front() != '/'
                              ? "/" + objectKey
                              : objectKey;
    return scheme + "://" + host + EncodePath(rawPath) + "?" +
           SignQuery(secretId, secretKey, method, rawPath, host, nowUnix, expireSec, true);
}

} // namespace utils
} // namespace UEAdminAPI

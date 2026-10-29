#pragma once

#include <string>

namespace UEAdminAPI {
namespace utils {

// 支付渠道用到的密码学原语（OpenSSL 3）。
// 私钥只在本模块内存中使用，不写日志、不返回给调用方。
class PaymentCrypto {
public:
    // 把控制台粘贴的公钥/私钥补齐 PEM 头尾并规范换行；已是 PEM 时只做换行归一。
    static std::string NormalizePem(const std::string &pem, const std::string &label);

    // RSA2 = SHA256withRSA，返回 Base64 签名。
    static std::string RsaSignSha256(const std::string &data, const std::string &privateKeyPem);

    // 校验 Base64 签名；任何解析/验签失败都返回 false。
    static bool RsaVerifySha256(const std::string &data,
                                const std::string &signatureBase64,
                                const std::string &publicKeyPem);

    // APIv3 回调密文解密：cipherWithTag 为密文 + 16 字节 GCM tag。
    static bool AesGcmDecrypt(const std::string &key,
                              const std::string &nonce,
                              const std::string &associatedData,
                              const std::string &cipherWithTag,
                              std::string &plaintext);

    static std::string RandomHex(size_t bytes);
};

} // namespace utils
} // namespace UEAdminAPI

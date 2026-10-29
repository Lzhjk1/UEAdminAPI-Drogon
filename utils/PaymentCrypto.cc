#include "PaymentCrypto.h"

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include <drogon/utils/Utilities.h>
#include <trantor/utils/Logger.h>

#include <algorithm>
#include <memory>
#include <random>
#include <sstream>

namespace UEAdminAPI {
namespace utils {

namespace {

struct PkeyDeleter {
    void operator()(EVP_PKEY *key) const { EVP_PKEY_free(key); }
};
struct BioDeleter {
    void operator()(BIO *bio) const { BIO_free(bio); }
};
struct CipherCtxDeleter {
    void operator()(EVP_CIPHER_CTX *ctx) const { EVP_CIPHER_CTX_free(ctx); }
};

std::unique_ptr<EVP_PKEY, PkeyDeleter> LoadPrivateKey(const std::string &pem)
{
    const std::string normalized = PaymentCrypto::NormalizePem(pem, "PRIVATE KEY");
    std::unique_ptr<BIO, BioDeleter> bio(BIO_new_mem_buf(normalized.data(), static_cast<int>(normalized.size())));
    if (!bio)
    {
        return nullptr;
    }
    return std::unique_ptr<EVP_PKEY, PkeyDeleter>(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
}

std::unique_ptr<EVP_PKEY, PkeyDeleter> LoadPublicKey(const std::string &pem)
{
    const std::string normalized = PaymentCrypto::NormalizePem(pem, "PUBLIC KEY");
    std::unique_ptr<BIO, BioDeleter> bio(BIO_new_mem_buf(normalized.data(), static_cast<int>(normalized.size())));
    if (!bio)
    {
        return nullptr;
    }
    return std::unique_ptr<EVP_PKEY, PkeyDeleter>(PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr));
}

std::string Base64LineWrap(const std::string &body)
{
    std::string out;
    out.reserve(body.size() + body.size() / 64 + 64);
    for (size_t i = 0; i < body.size(); i += 64)
    {
        out.append(body, i, std::min<size_t>(64, body.size() - i));
        out.push_back('\n');
    }
    return out;
}

} // namespace

std::string PaymentCrypto::NormalizePem(const std::string &pem, const std::string &label)
{
    if (pem.empty())
    {
        return "";
    }

    // 统一换行，去掉首尾空白
    std::string text;
    text.reserve(pem.size());
    for (char c : pem)
    {
        if (c != '\r')
        {
            text.push_back(c);
        }
    }
    const auto begin = text.find_first_not_of(" \t\n");
    if (begin == std::string::npos)
    {
        return "";
    }
    const auto end = text.find_last_not_of(" \t\n");
    text = text.substr(begin, end - begin + 1);

    if (text.find("-----BEGIN") != std::string::npos)
    {
        return text + "\n";
    }

    // 控制台拷贝出来的是纯 Base64，补上 PEM 头尾。
    // 部分控制台会连 "BEGIN PUBLIC KEY" 一起复制（没有连字符），一并剔除。
    std::string body;
    body.reserve(text.size());
    for (char c : text)
    {
        if (c != '\n' && c != ' ' && c != '\t' && c != '\r' && c != '-')
        {
            body.push_back(c);
        }
    }
    std::string labelCompact;
    for (char c : label)
    {
        if (c != ' ')
        {
            labelCompact.push_back(c);
        }
    }
    const std::string beginToken = "BEGIN" + labelCompact;
    const std::string endToken = "END" + labelCompact;
    if (body.rfind(beginToken, 0) == 0)
    {
        body.erase(0, beginToken.size());
    }
    if (body.size() >= endToken.size() &&
        body.compare(body.size() - endToken.size(), endToken.size(), endToken) == 0)
    {
        body.erase(body.size() - endToken.size());
    }
    return "-----BEGIN " + label + "-----\n" + Base64LineWrap(body) + "-----END " + label + "-----\n";
}

std::string PaymentCrypto::RsaSignSha256(const std::string &data, const std::string &privateKeyPem)
{
    auto key = LoadPrivateKey(privateKeyPem);
    if (!key)
    {
        LOG_ERROR << "[Payment] 私钥解析失败";
        return "";
    }

    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx || EVP_DigestSignInit(ctx.get(), nullptr, EVP_sha256(), nullptr, key.get()) != 1)
    {
        LOG_ERROR << "[Payment] 签名初始化失败";
        return "";
    }
    if (EVP_DigestSignUpdate(ctx.get(), data.data(), data.size()) != 1)
    {
        LOG_ERROR << "[Payment] 签名更新失败";
        return "";
    }
    size_t sigLen = 0;
    if (EVP_DigestSignFinal(ctx.get(), nullptr, &sigLen) != 1 || sigLen == 0)
    {
        LOG_ERROR << "[Payment] 签名长度计算失败";
        return "";
    }
    std::string signature(sigLen, '\0');
    if (EVP_DigestSignFinal(ctx.get(), reinterpret_cast<unsigned char *>(signature.data()), &sigLen) != 1)
    {
        LOG_ERROR << "[Payment] 签名计算失败";
        return "";
    }
    signature.resize(sigLen);
    return drogon::utils::base64Encode(std::string_view(signature));
}

bool PaymentCrypto::RsaVerifySha256(const std::string &data,
                                    const std::string &signatureBase64,
                                    const std::string &publicKeyPem)
{
    if (signatureBase64.empty())
    {
        return false;
    }
    auto key = LoadPublicKey(publicKeyPem);
    if (!key)
    {
        LOG_ERROR << "[Payment] 公钥解析失败";
        return false;
    }
    const std::string signature = drogon::utils::base64Decode(std::string_view(signatureBase64));
    if (signature.empty())
    {
        return false;
    }

    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx || EVP_DigestVerifyInit(ctx.get(), nullptr, EVP_sha256(), nullptr, key.get()) != 1)
    {
        return false;
    }
    if (EVP_DigestVerifyUpdate(ctx.get(), data.data(), data.size()) != 1)
    {
        return false;
    }
    return EVP_DigestVerifyFinal(ctx.get(),
                                 reinterpret_cast<const unsigned char *>(signature.data()),
                                 signature.size()) == 1;
}

bool PaymentCrypto::AesGcmDecrypt(const std::string &key,
                                  const std::string &nonce,
                                  const std::string &associatedData,
                                  const std::string &cipherWithTag,
                                  std::string &plaintext)
{
    constexpr size_t kTagLen = 16;
    if (key.size() != 32 || nonce.empty() || cipherWithTag.size() < kTagLen)
    {
        LOG_ERROR << "[Payment] AES-GCM 参数不合法: key=" << key.size() << ", nonce=" << nonce.size()
                  << ", cipher=" << cipherWithTag.size();
        return false;
    }

    const size_t cipherLen = cipherWithTag.size() - kTagLen;
    const unsigned char *cipher = reinterpret_cast<const unsigned char *>(cipherWithTag.data());
    const unsigned char *tag = cipher + cipherLen;

    std::unique_ptr<EVP_CIPHER_CTX, CipherCtxDeleter> ctx(EVP_CIPHER_CTX_new());
    if (!ctx)
    {
        return false;
    }

    if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
    {
        return false;
    }
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) != 1)
    {
        return false;
    }
    if (EVP_DecryptInit_ex(ctx.get(),
                           nullptr,
                           nullptr,
                           reinterpret_cast<const unsigned char *>(key.data()),
                           reinterpret_cast<const unsigned char *>(nonce.data())) != 1)
    {
        return false;
    }

    int len = 0;
    if (!associatedData.empty() &&
        EVP_DecryptUpdate(ctx.get(),
                          nullptr,
                          &len,
                          reinterpret_cast<const unsigned char *>(associatedData.data()),
                          static_cast<int>(associatedData.size())) != 1)
    {
        return false;
    }

    std::string out(cipherLen + kTagLen, '\0');
    int outLen = 0;
    if (EVP_DecryptUpdate(ctx.get(),
                          reinterpret_cast<unsigned char *>(out.data()),
                          &outLen,
                          cipher,
                          static_cast<int>(cipherLen)) != 1)
    {
        return false;
    }
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, static_cast<int>(kTagLen), const_cast<unsigned char *>(tag)) != 1)
    {
        return false;
    }
    int finalLen = 0;
    if (EVP_DecryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char *>(out.data()) + outLen, &finalLen) != 1)
    {
        LOG_WARN << "[Payment] AES-GCM 校验失败（密文被篡改或 APIv3 密钥不匹配）";
        return false;
    }
    out.resize(static_cast<size_t>(outLen + finalLen));
    plaintext = std::move(out);
    return true;
}

std::string PaymentCrypto::RandomHex(size_t bytes)
{
    static thread_local std::mt19937_64 rng(std::random_device{}());
    static const char *digits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes * 2);
    for (size_t i = 0; i < bytes; ++i)
    {
        const unsigned int v = static_cast<unsigned int>(rng() & 0xff);
        out.push_back(digits[v >> 4]);
        out.push_back(digits[v & 0x0f]);
    }
    return out;
}

} // namespace utils
} // namespace UEAdminAPI

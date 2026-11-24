#include "AiCrypto.h"

#include <drogon/drogon.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <array>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace UEAdminAPI::utils
{
namespace
{
constexpr int kKeySize = 32;
constexpr int kIvSize = 12;
constexpr int kTagSize = 16;

std::string EnvString(const char *name)
{
    const char *value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

std::string Base64Encode(const unsigned char *data, int length)
{
    if (length <= 0)
    {
        return "";
    }
    std::string out(4 * ((static_cast<size_t>(length) + 2) / 3), '\0');
    const int written = EVP_EncodeBlock(
        reinterpret_cast<unsigned char *>(out.data()), data, length);
    if (written <= 0)
    {
        return "";
    }
    out.resize(static_cast<size_t>(written));
    return out;
}

bool Base64Decode(const std::string &text, std::vector<unsigned char> &out)
{
    if (text.empty() || text.size() % 4 != 0)
    {
        return false;
    }
    out.assign(3 * (text.size() / 4), 0);
    const int written = EVP_DecodeBlock(out.data(),
                                        reinterpret_cast<const unsigned char *>(text.data()),
                                        static_cast<int>(text.size()));
    if (written < 0)
    {
        return false;
    }
    size_t length = static_cast<size_t>(written);
    if (!text.empty() && text.back() == '=')
    {
        --length;
    }
    if (text.size() >= 2 && text[text.size() - 2] == '=')
    {
        --length;
    }
    out.resize(length);
    return true;
}

bool HexToKey(const std::string &text, std::array<unsigned char, kKeySize> &key)
{
    if (text.size() != kKeySize * 2)
    {
        return false;
    }
    for (size_t i = 0; i < key.size(); ++i)
    {
        const auto high = text[i * 2];
        const auto low = text[i * 2 + 1];
        if (!std::isxdigit(static_cast<unsigned char>(high)) ||
            !std::isxdigit(static_cast<unsigned char>(low)))
        {
            return false;
        }
        key[i] = static_cast<unsigned char>(std::stoi(text.substr(i * 2, 2), nullptr, 16));
    }
    return true;
}

std::string Trim(const std::string &value)
{
    size_t begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin])))
    {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1])))
    {
        --end;
    }
    return value.substr(begin, end - begin);
}

bool DeriveKey(const std::string &secret, std::array<unsigned char, kKeySize> &key)
{
    if (secret.size() == kKeySize)
    {
        std::copy(secret.begin(), secret.end(), key.begin());
        return true;
    }
    const std::string trimmed = Trim(secret);
    if (trimmed.empty())
    {
        return false;
    }
    if (HexToKey(trimmed, key))
    {
        return true;
    }
    std::vector<unsigned char> decoded;
    if (Base64Decode(trimmed, decoded) && decoded.size() == kKeySize)
    {
        std::copy(decoded.begin(), decoded.end(), key.begin());
        return true;
    }
    if (trimmed.size() < 16)
    {
        return false;
    }
    SHA256(reinterpret_cast<const unsigned char *>(trimmed.data()), trimmed.size(),
           key.data());
    return true;
}

std::string JsonSecret()
{
    const auto custom = drogon::app().getCustomConfig();
    if (custom.isMember("AI") && custom["AI"].isObject() &&
        custom["AI"].isMember("Secret") && custom["AI"]["Secret"].isString())
    {
        return custom["AI"]["Secret"].asString();
    }
    return "";
}
} // namespace

bool LoadAiSecret(std::string &secret, std::string &error)
{
    const std::string value = !EnvString("UE_AI_SECRET").empty()
                                  ? EnvString("UE_AI_SECRET")
                                  : JsonSecret();
    if (value.empty())
    {
        error = "未配置 AI.Secret / UE_AI_SECRET，无法保存或读取加密的模型 Key";
        return false;
    }
    std::array<unsigned char, kKeySize> key{};
    if (!DeriveKey(value, key))
    {
        error = "AI.Secret / UE_AI_SECRET 太短；请使用至少 16 个字符，或 32 字节 Base64/64 位十六进制";
        return false;
    }
    secret.assign(reinterpret_cast<const char *>(key.data()), key.size());
    return true;
}

bool IsEncryptedAiKey(const std::string &value)
{
    return value.rfind("v1:", 0) == 0;
}

std::string EncryptAiKey(const std::string &plain, const std::string &secret)
{
    if (plain.empty())
    {
        return "";
    }
    std::array<unsigned char, kKeySize> key{};
    if (!DeriveKey(secret, key))
    {
        return "";
    }
    unsigned char iv[kIvSize];
    if (RAND_bytes(iv, kIvSize) != 1)
    {
        return "";
    }
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr)
    {
        return "";
    }
    std::vector<unsigned char> cipher(plain.size() + kTagSize);
    int length = 0;
    int total = 0;
    bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1;
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kIvSize, nullptr) == 1;
    ok = ok && EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), iv) == 1;
    ok = ok && EVP_EncryptUpdate(ctx, cipher.data(), &length,
                                 reinterpret_cast<const unsigned char *>(plain.data()),
                                 static_cast<int>(plain.size())) == 1;
    total = length;
    ok = ok && EVP_EncryptFinal_ex(ctx, cipher.data() + total, &length) == 1;
    total += length;
    unsigned char tag[kTagSize];
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kTagSize, tag) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok)
    {
        return "";
    }
    std::vector<unsigned char> blob;
    blob.insert(blob.end(), iv, iv + kIvSize);
    blob.insert(blob.end(), tag, tag + kTagSize);
    blob.insert(blob.end(), cipher.begin(), cipher.begin() + total);
    return "v1:" + Base64Encode(blob.data(), static_cast<int>(blob.size()));
}

bool DecryptAiKey(const std::string &stored, const std::string &secret, std::string &plain)
{
    plain.clear();
    if (stored.empty())
    {
        return false;
    }
    if (!IsEncryptedAiKey(stored))
    {
        // 兼容历史明文配置；写入新值时一律使用密文。
        plain = stored;
        return true;
    }
    std::vector<unsigned char> blob;
    if (!Base64Decode(stored.substr(3), blob) || blob.size() <= kIvSize + kTagSize)
    {
        return false;
    }
    std::array<unsigned char, kKeySize> key{};
    if (!DeriveKey(secret, key))
    {
        return false;
    }
    const unsigned char *iv = blob.data();
    const unsigned char *tag = blob.data() + kIvSize;
    const unsigned char *cipher = blob.data() + kIvSize + kTagSize;
    const int cipherLength = static_cast<int>(blob.size() - kIvSize - kTagSize);
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr)
    {
        return false;
    }
    std::vector<unsigned char> out(cipherLength + kTagSize);
    int length = 0;
    int total = 0;
    bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1;
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kIvSize, nullptr) == 1;
    ok = ok && EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), iv) == 1;
    ok = ok && EVP_DecryptUpdate(ctx, out.data(), &length, cipher, cipherLength) == 1;
    total = length;
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kTagSize,
                                   const_cast<unsigned char *>(tag)) == 1;
    ok = ok && EVP_DecryptFinal_ex(ctx, out.data() + total, &length) == 1;
    total += length;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok)
    {
        return false;
    }
    plain.assign(reinterpret_cast<const char *>(out.data()), static_cast<size_t>(total));
    return true;
}

std::string MaskAiKey(const std::string &plain)
{
    if (plain.empty())
    {
        return "";
    }
    if (plain.size() <= 8)
    {
        return "****";
    }
    const size_t head = plain.size() > 12 ? 6 : 2;
    return plain.substr(0, head) + "****" + plain.substr(plain.size() - 4);
}
} // namespace UEAdminAPI::utils

#pragma once

#include <string>

namespace UEAdminAPI::utils
{
// AI 服务端密钥加解密：上游 Key / 企业自带 Key 只在服务端以密文保存。
// 主密钥来自环境变量 UE_AI_SECRET（优先）或 custom_config.AI.Secret。
bool LoadAiSecret(std::string &secret, std::string &error);

std::string EncryptAiKey(const std::string &plain, const std::string &secret);
bool DecryptAiKey(const std::string &stored, const std::string &secret, std::string &plain);

bool IsEncryptedAiKey(const std::string &value);

// 列表展示用：sk-tp-****7f3a
std::string MaskAiKey(const std::string &plain);
} // namespace UEAdminAPI::utils

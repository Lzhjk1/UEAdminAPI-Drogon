#pragma once

#include <json/json.h>
#include <cstdint>
#include <map>
#include <string>

namespace UEAdminAPI {
namespace utils {

class PaymentUtils {
public:
    // 分 -> "12.34"（避免 ostringstream 受区域设置影响产生千分位）
    static std::string FormatYuan(int64_t cents);

    // "12.34" / "12" -> 1234；非法或精度超过 2 位返回 false
    static bool ParseYuanToCents(const std::string &yuan, int64_t &cents);

    static std::string JsonCompact(const Json::Value &value);

    // 解析 application/x-www-form-urlencoded（+ 视为空格）
    static std::map<std::string, std::string> ParseForm(const std::string &body);

    // "https://api.mch.weixin.qq.com/v3/xxx" -> base="https://api.mch.weixin.qq.com", path="/v3/xxx"
    static bool SplitUrl(const std::string &url, std::string &base, std::string &path);
};

} // namespace utils
} // namespace UEAdminAPI

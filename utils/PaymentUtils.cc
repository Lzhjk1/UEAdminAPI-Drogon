#include "PaymentUtils.h"

#include <drogon/utils/Utilities.h>

#include <cstdio>
#include <cstdlib>

namespace UEAdminAPI {
namespace utils {

std::string PaymentUtils::FormatYuan(int64_t cents)
{
    if (cents < 0)
    {
        cents = 0;
    }
    char buf[32] = {0};
    std::snprintf(buf, sizeof(buf), "%lld.%02lld",
                  static_cast<long long>(cents / 100),
                  static_cast<long long>(cents % 100));
    return std::string(buf);
}

bool PaymentUtils::ParseYuanToCents(const std::string &yuan, int64_t &cents)
{
    cents = 0;
    if (yuan.empty())
    {
        return false;
    }
    size_t pos = 0;
    while (pos < yuan.size() && yuan[pos] == ' ')
    {
        ++pos;
    }
    if (pos >= yuan.size() || yuan[pos] < '0' || yuan[pos] > '9')
    {
        return false;
    }

    int64_t whole = 0;
    while (pos < yuan.size() && yuan[pos] >= '0' && yuan[pos] <= '9')
    {
        whole = whole * 10 + (yuan[pos] - '0');
        if (whole > 100000000LL)
        {
            return false;
        }
        ++pos;
    }

    int frac = 0;
    int fracDigits = 0;
    if (pos < yuan.size() && yuan[pos] == '.')
    {
        ++pos;
        while (pos < yuan.size() && yuan[pos] >= '0' && yuan[pos] <= '9')
        {
            if (fracDigits >= 2)
            {
                return false;
            }
            frac = frac * 10 + (yuan[pos] - '0');
            ++fracDigits;
            ++pos;
        }
    }
    if (pos != yuan.size())
    {
        return false;
    }
    if (fracDigits == 1)
    {
        frac *= 10;
    }
    cents = whole * 100 + frac;
    return true;
}

std::string PaymentUtils::JsonCompact(const Json::Value &value)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["commentStyle"] = "None";
    std::string out = Json::writeString(builder, value);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
    {
        out.pop_back();
    }
    return out;
}

std::map<std::string, std::string> PaymentUtils::ParseForm(const std::string &body)
{
    std::map<std::string, std::string> params;
    size_t start = 0;
    while (start <= body.size())
    {
        auto amp = body.find('&', start);
        if (amp == std::string::npos)
        {
            amp = body.size();
        }
        const std::string pair = body.substr(start, amp - start);
        if (!pair.empty())
        {
            const auto eq = pair.find('=');
            std::string key = eq == std::string::npos ? pair : pair.substr(0, eq);
            std::string value = eq == std::string::npos ? "" : pair.substr(eq + 1);
            for (char &c : key)
            {
                if (c == '+')
                {
                    c = ' ';
                }
            }
            for (char &c : value)
            {
                if (c == '+')
                {
                    c = ' ';
                }
            }
            params[drogon::utils::urlDecode(key)] = drogon::utils::urlDecode(value);
        }
        if (amp == body.size())
        {
            break;
        }
        start = amp + 1;
    }
    return params;
}

bool PaymentUtils::SplitUrl(const std::string &url, std::string &base, std::string &path)
{
    base.clear();
    path.clear();
    const auto scheme = url.find("://");
    if (scheme == std::string::npos)
    {
        return false;
    }
    const auto slash = url.find('/', scheme + 3);
    if (slash == std::string::npos)
    {
        base = url;
        path = "/";
        return true;
    }
    base = url.substr(0, slash);
    path = url.substr(slash);
    if (path.empty())
    {
        path = "/";
    }
    return true;
}

} // namespace utils
} // namespace UEAdminAPI

#pragma once
#include <string>

namespace UEAdminAPI {
namespace utils {

enum class EnumThirdPartyPlatform {
    QQ = 1,
    WeChat = 2,
    None
};

std::string ThirdPartyPlatformToString(EnumThirdPartyPlatform platform, bool isLowerCase = false);
EnumThirdPartyPlatform getPlatformFromString(const std::string& platform);

}
}


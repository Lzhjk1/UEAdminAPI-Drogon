#include "utils/EnumThirdPartyPlatform.h"
#include "utils/DataFormatUtils.h"

namespace UEAdminAPI {
namespace utils {

std::string ThirdPartyPlatformToString(EnumThirdPartyPlatform platform, bool isLowerCase) {
    switch (platform) {
        case EnumThirdPartyPlatform::QQ:
            return isLowerCase ? "qq" : "QQ";
        case EnumThirdPartyPlatform::WeChat:
            return isLowerCase ? "wechat" : "WeChat";
        default:
            return "";
    }
}

EnumThirdPartyPlatform getPlatformFromString(const std::string& platform) {
    std::string platformLower = UEAdminAPI::DataFormatUtil::toLowerCase(platform);
    if (platformLower == "qq") {
        return EnumThirdPartyPlatform::QQ;
    } else if (platformLower == "wechat") {
        return EnumThirdPartyPlatform::WeChat;
    } else {
        return EnumThirdPartyPlatform::None;
    }
}

}
}


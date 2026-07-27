#include "HttpResult.h"

UEAdminAPI::utils::HttpResult::HttpResult(int32_t c, std::string m) {
    code = c;
    msg = m;
}

Json::Value UEAdminAPI::utils::HttpResult::toJson() const {
    Json::Value v;
    v["code"] = code;
    v["msg"] = msg;
    if (!jsondata.isNull()) {
        v["data"] = jsondata;
    }
    else {
        v["data"] = Json::objectValue;
    }
    return v;
}

std::string UEAdminAPI::utils::HttpResult::toJsonString() const {
    Json::FastWriter writer;
    return writer.write(toJson());
}

void UEAdminAPI::utils::HttpResult::setResult(int32_t c, std::string m) {
    code = c;
    msg = m;
}

void UEAdminAPI::utils::HttpResult::setResult(ApiErrorCode c) {
    code = static_cast<int32_t>(c);
    msg = GetApiErrorMessage(code);
}

void UEAdminAPI::utils::HttpResult::setResult(ApiErrorCode c, const std::string& customMsg) {
    code = static_cast<int32_t>(c);
    msg = customMsg;
}

#include "PostParamMap.h"
#include <drogon/HttpController.h>

namespace UEAdminAPI {

PostParamMap::PostParamMap(){
}

PostParamMap& PostParamMap::addParam(const std::string& name, bool isNecessary, const std::string& defaultValue) {
    param p;
    p.name = name;
    p.isExist = false;
    p.isNecessary = isNecessary;
    p.value = defaultValue;

    // 检查重复
    if (_mapParams.find(name) != _mapParams.end()) {
        throw std::runtime_error("Duplicated param : " + name);
    }

    _mapParams[name] = p;
    return *this;
}

void PostParamMap::setParamValue(const std::string& name, const std::string& value) {
    auto it = _mapParams.find(name);
    if (it != _mapParams.end()) {
        it->second.value = value;
        it->second.isExist = true;
    }
}

std::string PostParamMap::getParamValue(const std::string& name) const {
    auto it = _mapParams.find(name);
    if (it != _mapParams.end()) {
        return it->second.value;
    }
    return "";
}

std::vector<std::string> PostParamMap::checkRequiredParams() const {
    std::vector<std::string> missingFields;
    for (const auto &item : _mapParams) {
        if (item.second.isNecessary && !item.second.isExist) {
            missingFields.push_back(item.first);
        }
    }
    return missingFields;
}

void PostParamMap::readParamsFromJson(const Json::Value &json) {
    // 遍历请求json中的所有键值对
    // 如果键在_mapParams中存在，则将其值设置为请求json中的值
    for (const auto &item : _mapParams) {
        if (json.isMember(item.first)) {
            setParamValue(item.first, json[item.first].asString());
        }
    }
}

bool PostParamMap::hasParam(const std::string& name) const {
    auto it = _mapParams.find(name);
    return it != _mapParams.end() && it->second.isExist;
}

} // namespace UEAdminAPI
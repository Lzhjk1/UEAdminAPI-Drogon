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

PostParamMap &PostParamMap::addParamAsMutex(const std::string &name1, const std::string &name2, const std::string& defaultValue1, const std::string& defaultValue2) {
    addParam(name1, false, defaultValue1);
    addParam(name2, false, defaultValue2);

    _mutexParamGroups.push_back({name1, name2});
    return *this;
}

void PostParamMap::setParamValue(const std::string& name, const std::string& value) {
    auto it = _mapParams.find(name);
    if (it != _mapParams.end()) {
        it->second.value = value;
        it->second.isExist = true;
    }
}

std::string PostParamMap::getParam(const std::string& name) const {
    auto it = _mapParams.find(name);
    if (it != _mapParams.end()) {
        return it->second.value;
    }
    return "";
}

std::vector<std::string> PostParamMap::checkRequiredParams() const {
    std::vector<std::string> errors;
    
    // 检查必填参数
    for (const auto &item : _mapParams) {
        if (item.second.isNecessary && !item.second.isExist) {
            errors.push_back("缺少必填参数: " + item.first);
        }
    }
    
    // 检查互斥参数
    for (const auto& group : _mutexParamGroups) {
        const auto& paramA = _mapParams.at(group.first);
        const auto& paramB = _mapParams.at(group.second);
        
        // 检查两个参数是否同时存在
        if (paramA.isExist && paramB.isExist) {
            errors.push_back(group.first + " 和 " + group.second + " 不能同时提供");
        }
        // 检查两个参数是否都不存在
        else if (!paramA.isExist && !paramB.isExist) {
            errors.push_back(group.first + " 和 " + group.second + " 必须提供其中一个");
        }
    }
    
    return errors;
}

void PostParamMap::readParamsFromJson(const Json::Value &json) {
    // 遍历请求json中的所有键值对
    // 如果键在_mapParams中存在，则将其值设置为请求json中的值
    // 如果值为空或仅包含空格或制表符等空白字符, 则当做不存在该参数
    
    // 记录需要删除的键
    std::vector<std::string> keysToDelete;

    for (const auto &item : _mapParams) {
        if (json.isMember(item.first)) {
            std::string value = json[item.first].asString();
            // 清除其中的空白字符
            value.erase(std::remove_if(value.begin(), value.end(), ::isspace), value.end());
            if (!value.empty()) {
                setParamValue(item.first, value);
            }
            else{
                // 删除键值对
                keysToDelete.push_back(item.first);
            }
        }
    }
    // 删除键值对
    for (const auto& key : keysToDelete) {
        _mapParams.erase(key);
    }
}

bool PostParamMap::hasParam(const std::string& name) const {
    auto it = _mapParams.find(name);
    return it != _mapParams.end() && it->second.isExist;
}

} // namespace UEAdminAPI
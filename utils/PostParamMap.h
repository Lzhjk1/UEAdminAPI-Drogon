#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <json/json.h>
#include <drogon/HttpResponse.h>

namespace UEAdminAPI {
    // 参数结构体
    struct param {
        std::string value;
        bool isExist;
        bool isNecessary;
        std::string name;
    };

    class PostParamMap {
    private:
        std::unordered_map<std::string, param> _mapParams;

    public:
        // 构造函数
        PostParamMap();

        // 添加参数定义
        PostParamMap& addParam(const std::string& name, bool isNecessary, const std::string& defaultValue = "");

        // 设置参数值
        void setParamValue(const std::string& name, const std::string& value);

        // 获取参数值
        std::string getParamValue(const std::string& name) const;
        
        // 检查特定参数是否存在
        bool hasParam(const std::string& name) const;

        // 检查必填参数是否存在
        std::vector<std::string> checkRequiredParams() const;

        // 从json中读取需要的参数
        void readParamsFromJson(const Json::Value& json);
    };
}
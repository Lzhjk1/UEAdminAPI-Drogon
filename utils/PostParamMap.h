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

    // 互斥参数组结构体
    struct mutexParamGroup {
        std::string paramA;
        std::string paramB;
        std::string groupName;
    };

    /// @brief 用于简化从HttpRequestPtr读取参数的流程
    /// 1. 先实例化, 然后通过 addParam 方法添加接口所需要的参数, 可以指定参数是否必须, 以及默认值;
    /// 2. 然后通过 readParamsFromJson 传入 req->getJsonObject() 来读取body中的参数;
    /// 3. 最后通过 checkRequiredParams 方法检查参数是否完整, 该方法返回一个包含缺失参数名字的列表;
    class PostParamMap {
    private:
        std::unordered_map<std::string, param> _mapParams;
        std::vector<std::pair<std::string, std::string>> _mutexParamGroups;

    public:
        // 构造函数
        PostParamMap();

        // 添加参数定义
        PostParamMap& addParam(const std::string& name, bool isNecessar = false, const std::string& defaultValue = "");

        // 互斥参数, 例如: 互斥参数组为 A 和 B, 则 A 和 B 不能同时存在
        PostParamMap& addParamAsMutex(const std::string& name1, const std::string& name2, const std::string& defaultValue1 = "", const std::string& defaultValue2 = "");

        // 设置参数值
        void setParamValue(const std::string& name, const std::string& value);

        // 获取参数值
        std::string getParam(const std::string& name) const;
        
        // 检查特定参数是否存在
        bool hasParam(const std::string& name) const;

        // 检查必填参数是否存在和互斥参数是否满足条件, 返回错误信息列表
        std::vector<std::string> checkRequiredParams() const;

        // 从json中读取需要的参数
        void readParamsFromJson(const Json::Value& json);
    };
}
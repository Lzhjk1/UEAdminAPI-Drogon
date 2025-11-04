#pragma once

#include <string>
#include <json/json.h>
#include "utils/SingletonWithInit.h"

/**
 * @brief 示例服务类，展示如何使用SingletonWithInit模板
 */
class ExampleService : public SingletonWithInit<ExampleService> {
private:
    std::string _name;
    int _value;

    // 构造函数声明为私有，确保只能通过SingletonWithInit创建
    ExampleService(const std::string& name, int value);

public:
    // 示例方法
    std::string GetName() const { return _name; }
    int GetValue() const { return _value; }
    void SetValue(int value) { _value = value; }

    // 示例业务方法
    void DoSomething();
};

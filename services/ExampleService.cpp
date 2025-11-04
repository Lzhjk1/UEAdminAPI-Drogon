#include "ExampleService.h"
#include <iostream>

// 实现构造函数
ExampleService::ExampleService(const std::string& name, int value) 
    : _name(name), _value(value) {
    std::cout << "ExampleService initialized with name: " << name << ", value: " << value << std::endl;
}

// 实现DoSomething方法
void ExampleService::DoSomething() {
    std::cout << "ExampleService is doing something with name: " << _name << ", value: " << _value << std::endl;
}

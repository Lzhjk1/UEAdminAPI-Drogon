# SingletonWithInit 使用指南

## 概述

SingletonWithInit 是一个支持自定义初始化参数的单例模板类，允许不同服务类使用不同类型的初始化参数，同时保持单例模式的一致性。

## 基本用法

### 1. 继承 SingletonWithInit 模板

```cpp
class MyService : public SingletonWithInit<MyService> {
private:
    // 构造函数声明为私有，确保只能通过SingletonWithInit创建
    MyService(const std::string& name, int value);

public:
    // 公共方法
    void DoSomething();
};
```

### 2. 初始化服务

```cpp
// 使用自定义参数初始化
MyService::Init("MyService", 42);
```

### 3. 获取单例实例

```cpp
// 获取单例实例
MyService* service = MyService::Instance();
service->DoSomething();
```

## 实际示例

### 示例1: 使用Json::Value参数初始化

```cpp
class TencentSMSService : public ISmsService, public SingletonWithInit<TencentSMSService> {
private:
    TencentSMSService(const Json::Value& config);

public:
    // 业务方法...
};

// 使用方式
TencentSMSService::Init(configJson);
auto service = TencentSMSService::Instance();
```

### 示例2: 使用多个不同类型参数初始化

```cpp
class MFAService : public IMFAService, public SingletonWithInit<MFAService> {
private:
    MFAService(ISmsService* smsService, IEmailService* emailService);

public:
    // 业务方法...
};

// 使用方式
MFAService::Init(smsService, emailService);
auto service = MFAService::Instance();
```

### 示例3: 使用基本类型参数初始化

```cpp
class ExampleService : public SingletonWithInit<ExampleService> {
private:
    ExampleService(const std::string& name, int value);

public:
    // 业务方法...
};

// 使用方式
ExampleService::Init("Example", 100);
auto service = ExampleService::Instance();
```

## 注意事项

1. **必须先初始化**：在调用Instance()之前，必须先调用Init()进行初始化，否则会抛出异常。

2. **线程安全**：Init()方法是线程安全的，可以使用std::mutex保护初始化过程。

3. **单次初始化**：每个服务类只能初始化一次，后续调用Init()会直接返回true，不会重复初始化。

4. **参数类型**：Init()方法支持任意类型和数量的参数，通过完美转发(std::forward)传递给构造函数。

5. **构造函数私有**：确保构造函数声明为私有，防止直接实例化，只能通过SingletonWithInit创建。

## 总结

SingletonWithInit模板类提供了一种灵活、类型安全的方式来创建需要初始化参数的单例服务。通过可变参数模板，它支持各种类型的初始化参数，使代码更加模块化和可维护。

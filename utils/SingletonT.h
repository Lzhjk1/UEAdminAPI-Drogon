#pragma once

template <typename T>
class SingletonT {
public:
    // 禁止拷贝和赋值（单例不可复制）
    SingletonT(const SingletonT&) = delete;
    SingletonT& operator=(const SingletonT&) = delete;

    // 获取唯一实例（核心接口）
    static T& getInstance() {
        // 局部静态变量：C++11 及以上保证初始化线程安全，且仅初始化一次
        static T instance;
        return instance;
    }

protected:
    // 保护构造/析构：允许子类继承（如果需要），但禁止外部直接创建
    SingletonT() = default;
    ~SingletonT() = default;
};
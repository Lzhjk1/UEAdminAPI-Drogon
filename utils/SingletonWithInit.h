#pragma once

#include <mutex>
#include <stdexcept>
#include <utility>
#include <trantor/utils/Logger.h>

/**
 * @brief 单例模板类，支持自定义初始化参数
 * 
 * 使用方法:
 * 1. 继承此模板类，例如: class MyService : public SingletonWithInit<MyService>
 * 2. 实现构造函数，接收所需参数
 * 3. 使用Init(args...)进行初始化，然后通过Instance()获取单例
 * 
 * @tparam T 派生类类型
 */
template <typename T>
class SingletonWithInit {
protected:
    // 静态成员变量
    static bool s_initialized;
    static T* s_instance;
    static std::mutex s_mutex;

    // 构造函数保护，确保只能通过模板创建
    SingletonWithInit() = default;
    virtual ~SingletonWithInit() = default;

public:
    /**
     * @brief 初始化单例实例, 具体代码要查看类的构造函数
     * 
     * @tparam Args 参数类型
     * @param args 初始化参数
     * @return bool 初始化是否成功
     */
    template <typename... Args>
    static bool Init(Args&&... args) {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_initialized) {
            return true; // 已经初始化过
        }

        //try {
            s_instance = new T(std::forward<Args>(args)...);
            s_initialized = true;
            return true;
        //} catch (const std::exception& e) {
        //    throw e;
        //}
    }

    /**
     * @brief 获取单例实例
     * 
     * @return T* 单例实例指针
     * @throws std::runtime_error 如果未初始化
     */
    static T* Instance() {
        if (!s_initialized) {
            LOG_WARN << "Singleton not initialized. Call Init() first. Returning nullptr.";
            return nullptr;
        }
        return s_instance;
    }

    /**
     * @brief 检查单例是否已初始化
     * 
     * @return true 已初始化
     * @return false 未初始化
     */
    static bool IsInitialized() {
        return s_initialized;
    }

    // 禁止拷贝和赋值
    SingletonWithInit(const SingletonWithInit&) = delete;
    SingletonWithInit& operator=(const SingletonWithInit&) = delete;
};

// 模板静态成员初始化
template <typename T>
bool SingletonWithInit<T>::s_initialized = false;

template <typename T>
T* SingletonWithInit<T>::s_instance = nullptr;

template <typename T>
std::mutex SingletonWithInit<T>::s_mutex;

#pragma once
#include <random>
#include <cstdint>

class RandomGenerator {
private:
    static std::random_device rd;
    static std::mt19937 gen;

public:
    // 生成指定范围内的随机整数
    template<typename T>
    static T getInt(T min, T max) {
        std::uniform_int_distribution<T> dis(min, max);
        return dis(gen);
    }

    // 生成int32_t范围内的随机数
    static int32_t getInt32() {
        return getInt<int32_t>(INT32_MIN, INT32_MAX);
    }

    // 生成0到max-1之间的随机数
    template<typename T>
    static T getInt(T max) {
        return getInt<T>(0, max - 1);
    }

    // 生成指定位数的随机整数
    template<typename T>
    static T getRandNumber(T digits) {
        return getInt<T>(pow(10, digits - 1), pow(10, digits) - 1);
    }

    static std::string getRandNumberStr(int digits) {
        return std::to_string(getRandNumber<int>(digits));
    }
};
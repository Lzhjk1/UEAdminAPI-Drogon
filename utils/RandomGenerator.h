#pragma once
#include <random>
#include <cstdint>
#include <string>
#include <cmath>

class RandomGenerator {
private:
    static std::random_device rd;
    static std::mt19937 gen;

public:
    // 生成指定范围内的随机整数
    static int32_t getInt(int32_t min, int32_t max) {
        std::uniform_int_distribution<int32_t> dis(min, max);
        return dis(gen);
    }

    // 生成int64_t范围内的随机数
    static int64_t getInt64(int64_t min, int64_t max) {
        std::uniform_int_distribution<int64_t> dis(min, max);
        return dis(gen);
    }

    // 生成int32_t范围内的随机数
    static int32_t getInt32() {
        return getInt(INT32_MIN, INT32_MAX);
    }

    // 生成int64_t范围内的随机数
    static int64_t getInt64() {
        return getInt64(INT64_MIN, INT64_MAX);
    }

    // 生成0到max-1之间的随机数
    static int32_t getInt(int32_t max) {
        return getInt(0, max - 1);
    }

    // 生成0到max-1之间的随机数
    static int64_t getInt64(int64_t max) {
        return getInt64(0, max - 1);
    }

    // 生成指定位数的随机整数
    static int32_t getRandNumber(int32_t digits) {
        return getInt(static_cast<int32_t>(pow(10, digits - 1)), 
                     static_cast<int32_t>(pow(10, digits) - 1));
    }

    // 生成指定位数的随机整数
    static int64_t getRandNumber64(int32_t digits) {
        return getInt64(static_cast<int64_t>(pow(10, digits - 1)), 
                       static_cast<int64_t>(pow(10, digits) - 1));
    }

    static std::string getRandNumberStr(int digits) {
        return std::to_string(getRandNumber64(digits));
    }
};
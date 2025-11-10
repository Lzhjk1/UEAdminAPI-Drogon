#include "RandomGenerator.h"

// 静态成员定义
std::random_device RandomGenerator::rd;
std::mt19937 RandomGenerator::gen(RandomGenerator::rd());
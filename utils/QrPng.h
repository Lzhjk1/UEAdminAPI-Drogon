#pragma once

#include <string>

namespace UEAdminAPI {
namespace utils {

// 把任意文本渲染成 PNG 二维码（黑白、8 位灰度、带 4 模块静区）。
// 失败返回空字符串。
std::string RenderQrPng(const std::string &text, int scale = 6);

} // namespace utils
} // namespace UEAdminAPI

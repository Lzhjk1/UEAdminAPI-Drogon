#pragma once

#include <cstdint>
#include <string>

namespace UEAdminAPI {
namespace utils {

// 进程执行结果
struct ProcessResult {
    bool started = false;      // 进程是否成功启动
    bool timedOut = false;     // 是否因超时被强制终止
    int exitCode = -1;         // 退出码 (timedOut=true 时无意义)
    std::string errMsg;        // 启动/等待失败的原因
};

// 启动命令行 cmd 并等待最多 timeoutMs 毫秒, 超时则强制终止子进程.
// 跨平台: Windows 用 CreateProcessW, POSIX 暂为 stub (返回未实现错误).
// cmd 为 UTF-8 编码的完整命令行 (含可执行路径与参数).
ProcessResult runProcess(const std::string& cmd, uint32_t timeoutMs);

} // namespace utils
} // namespace UEAdminAPI

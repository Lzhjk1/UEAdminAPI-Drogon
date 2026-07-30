#include "ProcessRunner.h"

#include <trantor/utils/Logger.h>

namespace UEAdminAPI {
namespace utils {

#ifdef _WIN32

#include <windows.h>

ProcessResult runProcess(const std::string& cmd, uint32_t timeoutMs) {
    ProcessResult r;

    // UTF-8 → UTF-16 (避免中文路径/特殊字符问题)
    int wlen = MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), -1, NULL, 0);
    if (wlen <= 0) {
        r.errMsg = "MultiByteToWideChar 计算长度失败";
        return r;
    }
    std::wstring wcmd(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), -1, &wcmd[0], wlen);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof(pi));

    std::wstring wcmdMutable = wcmd;
    BOOL procOk = CreateProcessW(
        NULL, &wcmdMutable[0], NULL, NULL, FALSE,
        CREATE_NO_WINDOW, NULL, NULL, &si, &pi);

    if (!procOk) {
        r.errMsg = "CreateProcessW 失败, error=" + std::to_string(GetLastError());
        return r;
    }
    r.started = true;

    DWORD waitRet = WaitForSingleObject(pi.hProcess, timeoutMs);
    if (waitRet == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);  // 等待终止完成
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        r.timedOut = true;
        r.errMsg = "超时 " + std::to_string(timeoutMs) + "ms, 已强制终止";
        return r;
    } else if (waitRet == WAIT_FAILED) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        r.errMsg = "WaitForSingleObject 失败, error=" + std::to_string(GetLastError());
        return r;
    }

    // WAIT_OBJECT_0: 正常结束
    DWORD dwExit = 0;
    GetExitCodeProcess(pi.hProcess, &dwExit);
    r.exitCode = static_cast<int>(dwExit);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return r;
}

#else
// ---- POSIX stub: 确保非 Windows 平台可编译, 完整实现待 Linux 部署时补全 ----

ProcessResult runProcess(const std::string& cmd, uint32_t timeoutMs) {
    (void)cmd;
    (void)timeoutMs;
    ProcessResult r;
    r.errMsg = "runProcess 暂不支持非 Windows 平台 (POSIX 实现待补全)";
    LOG_WARN << "utils::runProcess: " << r.errMsg;
    return r;
}

#endif

} // namespace utils
} // namespace UEAdminAPI

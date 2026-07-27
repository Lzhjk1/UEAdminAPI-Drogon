#pragma once

#include <drogon/drogon.h>
#include <trantor/utils/Logger.h>

#include <chrono>
#include <coroutine>
#include <exception>
#include <functional>
#include <future>
#include <thread>
#include <type_traits>

namespace UEAdminAPI {
namespace utils {

/**
 * @brief 将阻塞操作投递到后台线程, 完成后在 Drogon IO 线程恢复协程.
 *
 * 用法:
 *   int result = co_await runOnBackground([&]() { return someBlockingIO(); });
 *
 * 注意:
 *   - 捕获的引用在 lambda 执行期间必须保持有效 (建议按值捕获或确保对象生命周期)
 *   - 异常会通过 co_await 重新抛出
 */
template <typename F>
class BackgroundAwaiter {
public:
    using ResultType = decltype(std::declval<F>()());

    explicit BackgroundAwaiter(F&& fn, std::mutex* mtx = nullptr)
        : _fn(std::forward<F>(fn)), _mtx(mtx) {}

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) {
        // 在后台线程执行阻塞操作
        std::thread([this, handle]() {
            if (_mtx) {
                _mtx->lock();
            }
            try {
                _result = _fn();
            } catch (...) {
                _exception = std::current_exception();
            }
            if (_mtx) {
                _mtx->unlock();
            }
            // 回到 Drogon IO 线程恢复协程
            drogon::app().getLoop()->queueInLoop(
                [handle]() { handle.resume(); });
        }).detach();
    }

    ResultType await_resume() {
        if (_exception) {
            std::rethrow_exception(_exception);
        }
        return std::move(_result);
    }

private:
    F _fn;
    std::mutex* _mtx;
    ResultType _result;
    std::exception_ptr _exception;
};

// 推导辅助: auto result = co_await runOnBackground([&](){ ... });
template <typename F>
auto runOnBackground(F&& fn, std::mutex* mtx = nullptr)
    -> BackgroundAwaiter<std::decay_t<F>> {
    return BackgroundAwaiter<std::decay_t<F>>(std::forward<F>(fn), mtx);
}

}  // namespace utils
}  // namespace UEAdminAPI

#pragma once

#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>
#include "services/MFAService.h"
#include "utils/TestModeConfig.h"

using namespace drogon;

/**
 * @brief 测试模式专用调试接口
 *
 * 仅当 custom_config.TestMode.Enable = true 时可用; 否则所有请求直接返回 403。
 * 路由:
 *   GET /api/test/mfa/latest?target={1}&type={2}  查询指定 target/type 的最新验证码
 */
class TestMfaController : public drogon::HttpController<TestMfaController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(TestMfaController::GetLatestCode, "/api/test/mfa/latest?target={1}&type={2}", Get);
    METHOD_LIST_END

    Task<HttpResponsePtr> GetLatestCode(HttpRequestPtr req, std::string target, std::string type);
};
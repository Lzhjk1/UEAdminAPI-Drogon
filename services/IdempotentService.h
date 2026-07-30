#pragma once

#include <drogon/drogon.h>
#include <json/json.h>
#include <string>
#include <optional>

namespace UEAdminAPI {
namespace Services {

/**
 * @brief 幂等去重服务
 *
 * 基于独立去重表 core.idempotent_keys, 实现"命中则直接返回上次结果,
 * 未命中则执行并缓存结果"的完整幂等语义.
 * 与 AuditLogService 职责分离: 幂等负责"返回上次结果", 审计负责"留痕".
 */
class IdempotentService {
public:
    IdempotentService() = delete;

    /**
     * @brief 查询幂等缓存
     *
     * @param requestId 幂等键 (requestId)
     * @return 命中返回上次响应的 JSON 字符串, 未命中返回 std::nullopt
     */
    static std::optional<std::string> get(const std::string& requestId);

    /**
     * @brief 写入幂等缓存
     *
     * 若 requestId 已存在则忽略(主键冲突视为已缓存, 返回 true).
     *
     * @param requestId 幂等键
     * @param responseJson 响应的 JSON 字符串
     * @return true 写入成功或已存在; false 写入失败
     */
    static bool put(const std::string& requestId, const std::string& responseJson);
};

} // namespace Services
} // namespace UEAdminAPI

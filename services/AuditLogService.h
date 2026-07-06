#pragma once

#include <drogon/drogon.h>
#include <json/json.h>
#include <string>
#include <optional>

namespace UEAdminAPI {
namespace Services {

/**
 * @brief 审计日志服务
 * 
 * 将 SQL RPC 操作记录到 core.audit_logs 表, 支持基于 audit_key 的幂等去重.
 * audit_key 由调用方提供 (通常为 requestId), 若同一 audit_key 已存在则跳过写入.
 */
class AuditLogService {
public:
    /**
     * @brief 记录一条审计日志
     * 
     * @param auditKey 幂等键, 非空时做去重检查 (相同 audit_key 不重复写入)
     * @param action 操作动作 (如 "sqlite.query", "sqlite.exec")
     * @param resultCode 结果码 (如 "0" 表示成功)
     * @param objectType 对象类型 (如 "sqlite_db")
     * @param objectId 对象标识 (如 logicalName)
     * @param detailJson 详细信息 JSON 字符串
     * @param userId 操作用户 ID, 0 表示未知/匿名
     * @param projectId 关联项目 ID, 0 表示无
     * @return true 写入成功或已存在(幂等); false 写入失败
     */
    static bool log(
        const std::string& auditKey,
        const std::string& action,
        const std::string& resultCode,
        const std::string& objectType,
        const std::string& objectId,
        const std::string& detailJson,
        int64_t userId = 0,
        int64_t projectId = 0);

    /**
     * @brief 检查 audit_key 是否已存在 (幂等去重)
     * 
     * @param auditKey 幂等键
     * @return true 已存在; false 不存在或查询失败(允许重复写入)
     */
    static bool existsByKey(const std::string& auditKey);

private:
    AuditLogService() = delete;
};

} // namespace Services
} // namespace UEAdminAPI

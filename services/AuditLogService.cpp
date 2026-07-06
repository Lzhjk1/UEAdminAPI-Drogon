#include "AuditLogService.h"
#include "AuditLogs.h"

#include <drogon/orm/Mapper.h>
#include <trantor/utils/Logger.h>

using namespace drogon;
using namespace drogon::orm;
using namespace drogon_model::UELocalDB::core;

namespace UEAdminAPI {
namespace Services {

bool AuditLogService::existsByKey(const std::string& auditKey) {
    if (auditKey.empty()) {
        return false;
    }

    try {
        auto dbClientPtr = app().getDbClient();
        if (!dbClientPtr) {
            return false;
        }

        Mapper<AuditLogs> mapper(dbClientPtr);
        auto rows = mapper.findBy(
            Criteria(AuditLogs::Cols::_audit_key, CompareOperator::EQ, auditKey));
        return !rows.empty();
    } catch (const std::exception& e) {
        LOG_WARN << "AuditLogService::existsByKey 查询失败: " << e.what();
        return false;
    }
}

bool AuditLogService::log(
    const std::string& auditKey,
    const std::string& action,
    const std::string& resultCode,
    const std::string& objectType,
    const std::string& objectId,
    const std::string& detailJson,
    int64_t userId,
    int64_t projectId) {

    // 幂等去重: 若 auditKey 非空且已存在, 跳过写入
    if (!auditKey.empty()) {
        if (existsByKey(auditKey)) {
            LOG_DEBUG << "AuditLogService: audit_key 已存在, 跳过写入: " << auditKey;
            return true;
        }
    }

    try {
        auto dbClientPtr = app().getDbClient();
        if (!dbClientPtr) {
            LOG_ERROR << "AuditLogService: DbClient 为空, 无法写入审计日志";
            return false;
        }

        AuditLogs row;

        if (!auditKey.empty()) {
            row.setAuditKey(auditKey);
        }

        row.setAction(action);

        if (!resultCode.empty()) {
            row.setResultCode(resultCode);
        }

        if (!objectType.empty()) {
            row.setObjectType(objectType);
        }

        if (!objectId.empty()) {
            row.setObjectId(objectId);
        }

        if (!detailJson.empty()) {
            row.setDetailJson(detailJson);
        }

        if (userId > 0) {
            row.setUserId(userId);
        } else {
            row.setUserIdToNull();
        }

        if (projectId > 0) {
            row.setProjectId(projectId);
        } else {
            row.setProjectIdToNull();
        }

        // tx_id 和 session_id 暂不关联
        row.setTxIdToNull();
        row.setSessionIdToNull();
        row.setBeforeJsonToNull();
        row.setAfterJsonToNull();

        Mapper<AuditLogs> mapper(dbClientPtr);
        mapper.insert(row);

        LOG_DEBUG << "AuditLogService: 审计日志已写入, action=" << action
                  << ", auditKey=" << auditKey;
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR << "AuditLogService::log 写入失败: " << e.what()
                  << ", action=" << action
                  << ", auditKey=" << auditKey;
        return false;
    }
}

} // namespace Services
} // namespace UEAdminAPI

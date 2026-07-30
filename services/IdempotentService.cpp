#include "IdempotentService.h"
#include "IdempotentKeys.h"

#include <drogon/orm/Mapper.h>
#include <trantor/utils/Logger.h>

using namespace drogon;
using namespace drogon::orm;
using namespace drogon_model::UELocalDB::core;

namespace UEAdminAPI {
namespace Services {

std::optional<std::string> IdempotentService::get(const std::string& requestId) {
    if (requestId.empty()) {
        return std::nullopt;
    }

    try {
        auto dbClientPtr = app().getDbClient();
        if (!dbClientPtr) {
            return std::nullopt;
        }

        Mapper<IdempotentKeys> mapper(dbClientPtr);
        auto rows = mapper.findBy(
            Criteria(IdempotentKeys::Cols::_request_id, CompareOperator::EQ, requestId));
        if (rows.empty()) {
            return std::nullopt;
        }
        return rows[0].getValueOfResponseJson();
    } catch (const std::exception& e) {
        LOG_WARN << "IdempotentService::get 查询失败: " << e.what();
        return std::nullopt;
    }
}

bool IdempotentService::put(const std::string& requestId, const std::string& responseJson) {
    if (requestId.empty()) {
        return false;
    }

    try {
        auto dbClientPtr = app().getDbClient();
        if (!dbClientPtr) {
            LOG_ERROR << "IdempotentService: DbClient 为空, 无法写入幂等缓存";
            return false;
        }

        IdempotentKeys row;
        row.setRequestId(requestId);
        row.setResponseJson(responseJson);

        Mapper<IdempotentKeys> mapper(dbClientPtr);
        mapper.insert(row);

        LOG_DEBUG << "IdempotentService: 幂等缓存已写入, requestId=" << requestId;
        return true;
    } catch (const std::exception& e) {
        // 主键冲突视为已存在, 返回 true
        LOG_DEBUG << "IdempotentService::put 写入失败(可能已存在): " << e.what();
        return true;
    }
}

} // namespace Services
} // namespace UEAdminAPI

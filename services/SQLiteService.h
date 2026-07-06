#pragma once

#include "utils/SingletonWithInit.h"
#include "utils/SQLite/SqliteConnection.h"
#include "utils/SQLite/SqliteRecordset.h"
#include "utils/SQLite/SqliteValue.h"
#include "utils/HttpResult.h"

#include <drogon/drogon.h>
#include <json/json.h>

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace UEAdminAPI {
namespace Services {

// SQL RPC 请求 DTO. 服务端约定只接收 SQLite 语法.
struct SqliteRpcRequest {
    std::string logicalName;                                    // 逻辑库名, 由客户端 resolve 后传入
    std::string sql;                                            // 必须是 SQLite 兼容 SQL, 含 ? 占位符
    std::vector<UEAdminAPI::SQLite::SqliteValue> params;         // 与 ? 一一对应, 顺序绑定
    std::string txId;                                           // 可选, 事务 token
    std::string requestId;                                      // 可选, 幂等键
    int64_t limit = -1;                                         // <0 表示不限制
    int64_t offset = 0;                                         // 翻页偏移
    int32_t timeoutMs = 0;                                      // 客户端期望的最长执行时间, 0 表示沿用连接默认
    bool readOnly = false;                                      // 客户端声明只读
    bool wantColumns = true;                                    // 是否返回列元信息
};

/**
 * @brief SQLiteService 是 PostgreSQL_Backend 与本地 SQLite 文件交互的统一入口.
 */
class SQLiteService : public SingletonWithInit<SQLiteService> {
    friend class SingletonWithInit<SQLiteService>;

public:
    SQLiteService(const Json::Value& config);
    ~SQLiteService();

    struct RouteInfo {
        bool found = false;
        std::string logicalName;
        std::string scope;
        std::string projectCode;
        std::string templateKind;
        std::string fileName;
        std::string sourceName;
        std::string sourcePath;
    };

    // ---- 连接池管理 ----
    UEAdminAPI::SQLite::SqliteConnectionPtr getConnection(const std::string& logicalName);
    UEAdminAPI::SQLite::SqliteConnectionPtr getDefaultConnection();
    void releaseConnection(const std::string& logicalName);
    void closeAll();

    // ---- 同步 SQL 入口, 供服务端内部直接调用 ----
    bool execute(const std::string& logicalName,
                 const std::string& sql,
                 const std::vector<UEAdminAPI::SQLite::SqliteValue>& params,
                 int64_t* affectedRows);

    UEAdminAPI::SQLite::SqliteRecordsetPtr query(
        const std::string& logicalName,
        const std::string& sql,
        const std::vector<UEAdminAPI::SQLite::SqliteValue>& params);

    // ---- 协程异步 SQL 入口, 面向 controller 层 ----
    drogon::Task<UEAdminAPI::utils::HttpResult> executeAsync(
        std::string logicalName,
        std::string sql,
        std::vector<UEAdminAPI::SQLite::SqliteValue> params);

    drogon::Task<UEAdminAPI::utils::HttpResult> queryAsync(
        std::string logicalName,
        std::string sql,
        std::vector<UEAdminAPI::SQLite::SqliteValue> params);

    // ---- SQL RPC 主入口, 由 SqliteController 直接调用 ----
    // 内部完成参数校验/事务路由/审计, 只接收 SQLite 语法 SQL.
    drogon::Task<UEAdminAPI::utils::HttpResult> handleQueryRpc(const SqliteRpcRequest& req);
    drogon::Task<UEAdminAPI::utils::HttpResult> handleExecuteRpc(const SqliteRpcRequest& req);

    // ---- 事务 token 管理 ----
    // 事务 token 是与 logicalName 绑定的字符串, 有效期由服务端控制.
    drogon::Task<UEAdminAPI::utils::HttpResult> beginTransaction(const std::string& logicalName);
    drogon::Task<UEAdminAPI::utils::HttpResult> commitTransaction(const std::string& txId);
    drogon::Task<UEAdminAPI::utils::HttpResult> rollbackTransaction(const std::string& txId);

    // ---- 逻辑库标识解析 ----
    // 输入 (scope, projectCode, templateKind, dbNode 等语义信息)返回 logicalName.
    // 简版实现: 按 "<scope>.<projectCode>.<templateKind>[.<fileName>]" 组合.
    drogon::Task<UEAdminAPI::utils::HttpResult> resolveLogicalName(
        const std::string& scope,
        const std::string& projectCode,
        const std::string& templateKind,
        const std::string& fileName);

    // ---- 健康检查 ----
    drogon::Task<UEAdminAPI::utils::HttpResult> ping();

    std::string rootDir() const { return _rootDir; }
    std::string defaultDbName() const { return _defaultDbName; }

private:
    // 逻辑名 -> 物理 .sqlite 文件路径.
    std::string resolveDbPath(const std::string& logicalName) const;

    // 基于 core.project_databases 查询路由.
    bool lookupRouteByLogicalName(const std::string& logicalName, RouteInfo* info) const;
    bool lookupRouteByRequest(const std::string& scope,
                              const std::string& projectCode,
                              const std::string& templateKind,
                              const std::string& fileName,
                              RouteInfo* info) const;
    static std::string buildFallbackLogicalName(const std::string& scope,
                                                const std::string& projectCode,
                                                const std::string& templateKind,
                                                const std::string& fileName);

    // 把 SqliteRecordset 转为 JSON 数组返回.
    Json::Value recordsetToJson(const UEAdminAPI::SQLite::SqliteRecordsetPtr& rs) const;

    // 统计 SQL 中 ? 占位符数量, 用于校验 params 数目匹配.
    static int countQuestionMarks(const std::string& sql);

    // 生成/校验事务 token
    std::string newTxToken();

    // 事务表: txId -> (logicalName, expiresAt)
    struct TxInfo {
        std::string logicalName;
        std::chrono::steady_clock::time_point expiresAt;
    };

private:
    std::string _rootDir;
    std::string _defaultDbName;

    mutable std::mutex _poolMutex;
    std::map<std::string, UEAdminAPI::SQLite::SqliteConnectionPtr> _pool;

    mutable std::mutex _txMutex;
    std::map<std::string, TxInfo> _activeTx;                    // txId -> info
    std::map<std::string, std::string> _logicalToTx;             // logicalName -> txId, 用于防止同库多事务
    int32_t _txTimeoutSec = 60;                                 // 事务硬超时
};

}  // namespace Services
}  // namespace UEAdminAPI

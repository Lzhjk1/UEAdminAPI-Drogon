#pragma once

#include "utils/SingletonWithInit.h"
#include "utils/SQLite/SqliteConnection.h"
#include "utils/SQLite/SqliteRecordset.h"
#include "utils/SQLite/SqliteValue.h"
#include "utils/HttpResult.h"

#include <drogon/drogon.h>
#include <json/json.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace UEAdminAPI {
namespace Services {

/**
 * @brief SQLiteService 是 PostgreSQL_Backend 与本地 SQLite 文件交互的统一入口.
 *
 * 角色定位:
 *  - 维护 (逻辑名 -> SqliteConnection) 的连接池, 与桌面端"项目 + 模板"路由模型对齐;
 *  - 所有对 SQLite 的访问统一从这里获取连接, 调用方不直接管理 sqlite3 句柄;
 *  - 默认使用 std::mutex + SqliteConnection 内部的 mutex 双层保护, 串行写, 并发读;
 *  - 对外提供 sync / async 两套接口: 异步 API 在工作线程中执行 SQL, 返回 drogon::Task,
 *    避免阻塞 Drogon 的事件循环线程.
 *
 * 配置:
 *  - 从 config 的 "SQLite" 节点读取 root_dir 与 default_db_name;
 *  - root_dir 为相对路径时, 以可执行文件目录为基准展开;
 *  - default_db_name 在未指定逻辑名时使用.
 */
class SQLiteService : public SingletonWithInit<SQLiteService> {
    friend class SingletonWithInit<SQLiteService>;

public:
    SQLiteService(const Json::Value& config);
    ~SQLiteService();

    /**
     * @brief 取得指定逻辑名对应的连接, 若不存在则按 (root_dir / name + ".sqlite") 打开.
     * @return 失败返回空指针, 错误信息可由调用方再次调用 lastError 获取.
     */
    UEAdminAPI::SQLite::SqliteConnectionPtr getConnection(const std::string& logicalName);

    /**
     * @brief 取得默认逻辑库的连接, 等价于 getConnection(默认名).
     */
    UEAdminAPI::SQLite::SqliteConnectionPtr getDefaultConnection();

    /**
     * @brief 关闭并释放指定逻辑名对应的连接.
     */
    void releaseConnection(const std::string& logicalName);

    /**
     * @brief 关闭所有连接, 服务关闭/重启时调用.
     */
    void closeAll();

    /**
     * @brief 同步执行写操作.
     */
    bool execute(const std::string& logicalName,
                 const std::string& sql,
                 const std::vector<UEAdminAPI::SQLite::SqliteValue>& params,
                 int64_t* affectedRows);

    /**
     * @brief 同步执行查询, 返回完整记录集.
     */
    UEAdminAPI::SQLite::SqliteRecordsetPtr query(
        const std::string& logicalName,
        const std::string& sql,
        const std::vector<UEAdminAPI::SQLite::SqliteValue>& params);

    /**
     * @brief 异步执行写操作, 在 Drogon 的全局工作线程中实际执行 SQL.
     *        返回结果 jsondata 包含 affectedRows 与 ok 字段.
     */
    drogon::Task<UEAdminAPI::utils::HttpResult> executeAsync(
        std::string logicalName,
        std::string sql,
        std::vector<UEAdminAPI::SQLite::SqliteValue> params);

    /**
     * @brief 异步执行查询, jsondata 中含 rows(JSON 数组).
     */
    drogon::Task<UEAdminAPI::utils::HttpResult> queryAsync(
        std::string logicalName,
        std::string sql,
        std::vector<UEAdminAPI::SQLite::SqliteValue> params);

    /**
     * @brief 简单健康检查: 在默认连接上执行 "SELECT 1".
     */
    drogon::Task<UEAdminAPI::utils::HttpResult> ping();

    // 配置只读访问, 方便诊断接口
    std::string rootDir() const { return _rootDir; }
    std::string defaultDbName() const { return _defaultDbName; }

private:
    // 解析逻辑名 -> 实际文件路径; 若 logicalName 为空使用 default
    std::string resolveDbPath(const std::string& logicalName) const;

    // 把 SqliteRecordset 转 JSON 数组, 适合在 HTTP 接口里返回
    Json::Value recordsetToJson(const UEAdminAPI::SQLite::SqliteRecordsetPtr& rs) const;

private:
    std::string _rootDir;          // SQLite 文件根目录(绝对路径)
    std::string _defaultDbName;    // 缺省逻辑库名

    mutable std::mutex _poolMutex;
    std::map<std::string, UEAdminAPI::SQLite::SqliteConnectionPtr> _pool;
};

}  // namespace Services
}  // namespace UEAdminAPI

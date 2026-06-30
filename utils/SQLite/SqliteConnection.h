#pragma once

#include "SqliteRecordset.h"
#include "SqliteValue.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct sqlite3;  // 前向声明, 避免在 .h 中暴露 sqlite3.h

namespace UEAdminAPI {
namespace SQLite {

/**
 * @brief 单个 SQLite 数据库文件的连接封装.
 *
 * 线程模型:
 *  - 每个 SqliteConnection 内部用 std::mutex 串行化所有写操作;
 *  - SQLite 在 WAL 模式下允许并发读, 但 sqlite3_stmt 不是线程安全的,
 *    故所有调用都加锁; 调用方可以构造多个连接对象实现并行读.
 *
 * 错误处理:
 *  - 所有方法不抛 sqlite 异常, 错误经 lastError() 取回;
 *  - 出错时返回 false 或空指针, 调用方负责回滚事务.
 */
class SqliteConnection {
public:
    SqliteConnection();
    ~SqliteConnection();

    // 禁止拷贝, 允许移动语义留待未来扩展
    SqliteConnection(const SqliteConnection&) = delete;
    SqliteConnection& operator=(const SqliteConnection&) = delete;

    /**
     * @brief 打开指定路径的 .sqlite 文件, 不存在则创建.
     * @param dbPath UTF-8 数据库文件路径
     * @return 成功打开返回 true
     */
    bool open(const std::string& dbPath);

    /**
     * @brief 关闭连接, 重复调用安全
     */
    void close();

    bool isOpen() const;

    /**
     * @brief 执行无返回值 SQL (INSERT/UPDATE/DELETE/DDL).
     * @param sql SQL 语句, 可以包含 ? 占位符
     * @param params 与 ? 对应的参数, 顺序绑定; 没有参数传空 vector
     * @param affectedRows 输出: 影响行数, 可为空指针
     */
    bool execute(const std::string& sql,
                 const std::vector<SqliteValue>& params,
                 int64_t* affectedRows);

    /**
     * @brief 执行 SELECT, 一次性读完整结果集, 不持有 stmt.
     * @return 出错或语句不合法时返回空指针
     */
    SqliteRecordsetPtr query(const std::string& sql,
                             const std::vector<SqliteValue>& params);

    /**
     * @brief 执行 SELECT 但仅读取第一行 (常用于计数/简单查询).
     * @param row 输出: 第一行内容; 无结果时 row 不被修改
     * @return 是否读到数据
     */
    bool queryFirstRow(const std::string& sql,
                       const std::vector<SqliteValue>& params,
                       SqliteRow& row);

    /**
     * @brief 执行一段不带参数, 不需返回的脚本 (多条语句以 ; 分隔), 适合初始化 schema.
     */
    bool executeScript(const std::string& script);

    // 事务: 简单的串行事务, 不支持嵌套
    bool beginTransaction();
    bool commitTransaction();
    bool rollbackTransaction();

    // 最近一次错误描述, 空字符串表示无错误
    std::string lastError() const;

    // 获取底层 sqlite3 句柄, 仅供测试/扩展使用; 调用者不得 close
    sqlite3* rawHandle() const { return _db; }

private:
    // 应用通用 PRAGMA, 与 ObjectPRX 桌面端保持一致
    void applyDefaultPragmas();

    void setLastError(const std::string& msg);

    // sqlite3_step 出错时统一记录 errmsg
    bool bindParams(void* stmt, const std::vector<SqliteValue>& params);

private:
    mutable std::mutex _mutex;
    sqlite3* _db;
    std::string _lastError;
};

typedef std::shared_ptr<SqliteConnection> SqliteConnectionPtr;

}  // namespace SQLite
}  // namespace UEAdminAPI

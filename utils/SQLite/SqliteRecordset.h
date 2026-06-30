#pragma once

#include "SqliteValue.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace UEAdminAPI {
namespace SQLite {

/**
 * @brief 一行记录, 以列名(小写)为键, 值为 SqliteValue.
 */
typedef std::unordered_map<std::string, SqliteValue> SqliteRow;

/**
 * @brief 已经从数据库读取到内存的查询结果集.
 *
 * 设计要点:
 *  - 结果在 Recordset 构造时已全量拉取, 之后与底层 sqlite3 句柄解耦,
 *    便于在协程/线程间安全传递, 不会出现 dangling stmt 风险;
 *  - 列名统一保留原始大小写, 同时记录小写映射, 方便业务用任意大小写访问.
 */
class SqliteRecordset {
public:
    SqliteRecordset();

    // 列管理: 仅在构造阶段使用, 业务侧只读
    void addColumn(const std::string& columnName);
    void addRow(const SqliteRow& row);
    void addRow(SqliteRow&& row);

    // 完成数据装载, 计算辅助索引
    void finalize();

    // 列与行
    int columnCount() const { return static_cast<int>(_columns.size()); }
    int rowCount() const { return static_cast<int>(_rows.size()); }
    bool isEmpty() const { return _rows.empty(); }

    const std::string& columnName(int index) const;
    const std::vector<std::string>& columnNames() const { return _columns; }

    const SqliteRow& row(int index) const;
    const std::vector<SqliteRow>& rows() const { return _rows; }

    // 值访问: 用列名访问(大小写不敏感), 列不存在返回 null 值
    const SqliteValue& valueAt(int rowIndex, const std::string& columnName) const;

private:
    std::vector<std::string> _columns;          // 列名(原始大小写)
    std::vector<SqliteRow> _rows;               // 行数据(键为小写列名)
    SqliteValue _nullValue;                     // 缺省值, 用于越界访问
};

typedef std::shared_ptr<SqliteRecordset> SqliteRecordsetPtr;

}  // namespace SQLite
}  // namespace UEAdminAPI

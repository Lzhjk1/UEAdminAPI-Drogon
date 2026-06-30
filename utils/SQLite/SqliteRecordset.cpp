#include "SqliteRecordset.h"

#include <algorithm>
#include <cctype>

namespace UEAdminAPI {
namespace SQLite {

namespace {
// 列名归一化: 全小写, 用于 row 内部键
std::string toLower(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        r.push_back(static_cast<char>(std::tolower(c)));
    }
    return r;
}
}  // namespace

SqliteRecordset::SqliteRecordset() {}

void SqliteRecordset::addColumn(const std::string& columnName) {
    _columns.push_back(columnName);
}

void SqliteRecordset::addRow(const SqliteRow& row) {
    _rows.push_back(row);
}

void SqliteRecordset::addRow(SqliteRow&& row) {
    _rows.push_back(std::move(row));
}

void SqliteRecordset::finalize() {
    // 当前实现不需要额外索引, 保留接口便于以后扩展(如建立列名->索引映射)
}

const std::string& SqliteRecordset::columnName(int index) const {
    static const std::string empty;
    if (index < 0 || index >= static_cast<int>(_columns.size())) {
        return empty;
    }
    return _columns[index];
}

const SqliteRow& SqliteRecordset::row(int index) const {
    static const SqliteRow empty;
    if (index < 0 || index >= static_cast<int>(_rows.size())) {
        return empty;
    }
    return _rows[index];
}

const SqliteValue& SqliteRecordset::valueAt(int rowIndex, const std::string& columnName) const {
    if (rowIndex < 0 || rowIndex >= static_cast<int>(_rows.size())) {
        return _nullValue;
    }
    const std::string key = toLower(columnName);
    const SqliteRow& r = _rows[rowIndex];
    SqliteRow::const_iterator it = r.find(key);
    if (it == r.end()) {
        return _nullValue;
    }
    return it->second;
}

}  // namespace SQLite
}  // namespace UEAdminAPI

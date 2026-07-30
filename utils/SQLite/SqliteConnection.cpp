#include "SqliteConnection.h"

#include <sqlite3.h>
#include <trantor/utils/Logger.h>

#include <algorithm>
#include <cctype>
#include <chrono>

namespace UEAdminAPI {
namespace SQLite {

namespace {
// 列名归一化为小写, 与 SqliteRecordset 内部保持一致
std::string toLower(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        r.push_back(static_cast<char>(std::tolower(c)));
    }
    return r;
}

// 从 sqlite3_stmt 当前行中读出一个列值, 自动按列类型分发
SqliteValue readColumnValue(sqlite3_stmt* stmt, int col) {
    int type = sqlite3_column_type(stmt, col);
    switch (type) {
    case SQLITE_INTEGER:
        return SqliteValue::FromInt(sqlite3_column_int64(stmt, col));
    case SQLITE_FLOAT:
        return SqliteValue::FromReal(sqlite3_column_double(stmt, col));
    case SQLITE_TEXT: {
        const unsigned char* txt = sqlite3_column_text(stmt, col);
        int bytes = sqlite3_column_bytes(stmt, col);
        if (txt == nullptr || bytes <= 0) {
            return SqliteValue::FromText(std::string());
        }
        return SqliteValue::FromText(std::string(reinterpret_cast<const char*>(txt), bytes));
    }
    case SQLITE_BLOB: {
        const void* data = sqlite3_column_blob(stmt, col);
        int bytes = sqlite3_column_bytes(stmt, col);
        if (data == nullptr || bytes <= 0) {
            return SqliteValue::FromBlob(std::vector<uint8_t>());
        }
        const uint8_t* p = static_cast<const uint8_t*>(data);
        return SqliteValue::FromBlob(std::vector<uint8_t>(p, p + bytes));
    }
    case SQLITE_NULL:
    default:
        return SqliteValue::FromNull();
    }
}
}  // namespace

SqliteConnection::SqliteConnection()
    : _db(nullptr) {}

SqliteConnection::~SqliteConnection() {
    close();
}

bool SqliteConnection::open(const std::string& dbPath) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_db != nullptr) {
        setLastError("connection already opened");
        return false;
    }
    sqlite3* db = nullptr;
    int rc = sqlite3_open_v2(dbPath.c_str(), &db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                             nullptr);
    if (rc != SQLITE_OK) {
        std::string msg = (db != nullptr) ? std::string(sqlite3_errmsg(db)) : std::string("sqlite3_open_v2 failed");
        if (db != nullptr) {
            sqlite3_close(db);
        }
        setLastError(msg);
        LOG_ERROR << "SqliteConnection::open 失败: " << msg << ", path=" << dbPath;
        return false;
    }
    _db = db;
    applyDefaultPragmas();
    LOG_INFO << "SqliteConnection::open 打开成功: " << dbPath;
    return true;
}

void SqliteConnection::close() {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_db != nullptr) {
        sqlite3_close(_db);
        _db = nullptr;
    }
}

bool SqliteConnection::isOpen() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _db != nullptr;
}

void SqliteConnection::applyDefaultPragmas() {
    // 与桌面端保持一致, 这里是 open() 内部调用, 已经持有 _mutex
    const char* pragmas[] = {
        "PRAGMA encoding = 'UTF-8';",
        "PRAGMA journal_mode = WAL;",
        "PRAGMA synchronous = NORMAL;",
        "PRAGMA temp_store = MEMORY;",
        "PRAGMA foreign_keys = ON;"
    };
    for (size_t i = 0; i < sizeof(pragmas) / sizeof(pragmas[0]); ++i) {
        char* err = nullptr;
        int rc = sqlite3_exec(_db, pragmas[i], nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            LOG_WARN << "PRAGMA 执行失败: " << pragmas[i]
                     << ", err=" << (err ? err : "(null)");
            if (err) sqlite3_free(err);
        }
    }
    sqlite3_busy_timeout(_db, 5000);
}

void SqliteConnection::setLastError(const std::string& msg) {
    _lastError = msg;
}

std::string SqliteConnection::lastError() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _lastError;
}

bool SqliteConnection::bindParams(void* stmtPtr, const std::vector<SqliteValue>& params) {
    sqlite3_stmt* stmt = static_cast<sqlite3_stmt*>(stmtPtr);
    for (size_t i = 0; i < params.size(); ++i) {
        // SQLite 占位符索引从 1 开始
        int idx = static_cast<int>(i) + 1;
        const SqliteValue& v = params[i];
        int rc = SQLITE_OK;
        switch (v.type()) {
        case SqliteValue::vtNull:
            rc = sqlite3_bind_null(stmt, idx);
            break;
        case SqliteValue::vtInt:
            rc = sqlite3_bind_int64(stmt, idx, v.asInt());
            break;
        case SqliteValue::vtReal:
            rc = sqlite3_bind_double(stmt, idx, v.asReal());
            break;
        case SqliteValue::vtText: {
            const std::string& t = v.asText();
            // SQLITE_TRANSIENT 让 SQLite 自行拷贝, 避免 t 生命周期问题
            rc = sqlite3_bind_text(stmt, idx, t.c_str(), static_cast<int>(t.size()), SQLITE_TRANSIENT);
            break;
        }
        case SqliteValue::vtBlob: {
            const std::vector<uint8_t>& b = v.asBlob();
            if (b.empty()) {
                rc = sqlite3_bind_zeroblob(stmt, idx, 0);
            } else {
                rc = sqlite3_bind_blob(stmt, idx, b.data(), static_cast<int>(b.size()), SQLITE_TRANSIENT);
            }
            break;
        }
        }
        if (rc != SQLITE_OK) {
            setLastError(std::string("bind param failed at index ") + std::to_string(idx)
                         + ": " + sqlite3_errmsg(_db));
            return false;
        }
    }
    return true;
}

bool SqliteConnection::execute(const std::string& sql,
                               const std::vector<SqliteValue>& params,
                               int64_t* affectedRows,
                               int timeoutMs) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_db == nullptr) {
        setLastError("database not opened");
        return false;
    }

    // 注册超时回调, 在 sqlite3_step 内部每隔约 100 条虚拟机指令检查一次
    if (timeoutMs > 0) {
        auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(timeoutMs);
        sqlite3_progress_handler(_db, 100, [](void* ptr) -> int {
            auto* dp = static_cast<std::chrono::steady_clock::time_point*>(ptr);
            return std::chrono::steady_clock::now() >= *dp ? 1 : 0;
        }, &deadline);
    }

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        setLastError(std::string("prepare failed: ") + sqlite3_errmsg(_db));
        if (stmt) sqlite3_finalize(stmt);
        if (timeoutMs > 0) sqlite3_progress_handler(_db, 0, nullptr, nullptr);
        return false;
    }
    if (!bindParams(stmt, params)) {
        sqlite3_finalize(stmt);
        if (timeoutMs > 0) sqlite3_progress_handler(_db, 0, nullptr, nullptr);
        return false;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        setLastError(std::string("step failed: ") + sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        if (timeoutMs > 0) sqlite3_progress_handler(_db, 0, nullptr, nullptr);
        return false;
    }
    if (affectedRows != nullptr) {
        *affectedRows = static_cast<int64_t>(sqlite3_changes(_db));
    }
    sqlite3_finalize(stmt);
    if (timeoutMs > 0) sqlite3_progress_handler(_db, 0, nullptr, nullptr);
    setLastError(std::string());
    return true;
}

SqliteRecordsetPtr SqliteConnection::query(const std::string& sql,
                                           const std::vector<SqliteValue>& params,
                                           int timeoutMs) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_db == nullptr) {
        setLastError("database not opened");
        return SqliteRecordsetPtr();
    }

    // 注册超时回调 (同 execute)
    if (timeoutMs > 0) {
        auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(timeoutMs);
        sqlite3_progress_handler(_db, 100, [](void* ptr) -> int {
            auto* dp = static_cast<std::chrono::steady_clock::time_point*>(ptr);
            return std::chrono::steady_clock::now() >= *dp ? 1 : 0;
        }, &deadline);
    }

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        setLastError(std::string("prepare failed: ") + sqlite3_errmsg(_db));
        if (stmt) sqlite3_finalize(stmt);
        if (timeoutMs > 0) sqlite3_progress_handler(_db, 0, nullptr, nullptr);
        return SqliteRecordsetPtr();
    }
    if (!bindParams(stmt, params)) {
        sqlite3_finalize(stmt);
        if (timeoutMs > 0) sqlite3_progress_handler(_db, 0, nullptr, nullptr);
        return SqliteRecordsetPtr();
    }

    SqliteRecordsetPtr rs(new SqliteRecordset());
    int colCount = sqlite3_column_count(stmt);
    // 准备列名表
    std::vector<std::string> lowerNames;
    lowerNames.reserve(colCount);
    for (int c = 0; c < colCount; ++c) {
        const char* name = sqlite3_column_name(stmt, c);
        std::string colName = (name != nullptr) ? name : std::string();
        rs->addColumn(colName);
        lowerNames.push_back(toLower(colName));
    }

    // 拉取所有行
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        SqliteRow row;
        row.reserve(static_cast<size_t>(colCount));
        for (int c = 0; c < colCount; ++c) {
            row[lowerNames[c]] = readColumnValue(stmt, c);
        }
        rs->addRow(std::move(row));
    }
    if (rc != SQLITE_DONE) {
        setLastError(std::string("step failed: ") + sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        if (timeoutMs > 0) sqlite3_progress_handler(_db, 0, nullptr, nullptr);
        return SqliteRecordsetPtr();
    }
    sqlite3_finalize(stmt);
    if (timeoutMs > 0) sqlite3_progress_handler(_db, 0, nullptr, nullptr);
    rs->finalize();
    setLastError(std::string());
    return rs;
}

bool SqliteConnection::queryFirstRow(const std::string& sql,
                                     const std::vector<SqliteValue>& params,
                                     SqliteRow& row) {
    SqliteRecordsetPtr rs = query(sql, params);
    if (!rs || rs->isEmpty()) {
        return false;
    }
    row = rs->row(0);
    return true;
}

bool SqliteConnection::executeScript(const std::string& script) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_db == nullptr) {
        setLastError("database not opened");
        return false;
    }
    char* err = nullptr;
    int rc = sqlite3_exec(_db, script.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = (err != nullptr) ? std::string(err) : std::string("unknown error");
        if (err) sqlite3_free(err);
        setLastError(msg);
        return false;
    }
    setLastError(std::string());
    return true;
}

bool SqliteConnection::beginTransaction() {
    return execute("BEGIN;", std::vector<SqliteValue>(), nullptr);
}

bool SqliteConnection::commitTransaction() {
    return execute("COMMIT;", std::vector<SqliteValue>(), nullptr);
}

bool SqliteConnection::rollbackTransaction() {
    return execute("ROLLBACK;", std::vector<SqliteValue>(), nullptr);
}

}  // namespace SQLite
}  // namespace UEAdminAPI

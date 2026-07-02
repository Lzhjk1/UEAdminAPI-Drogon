#include "SQLiteService.h"

#include "utils/ApiErrorCodes.h"

#include <trantor/utils/Logger.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <random>
#include <sstream>

using namespace UEAdminAPI;
using namespace UEAdminAPI::SQLite;
using namespace UEAdminAPI::utils;

namespace UEAdminAPI {
namespace Services {

namespace {
// 去除前后引号与空白, 适配 yaml/json 不同写法
std::string stripQuotesAndSpaces(const std::string& s) {
    std::string r = s;
    auto isSpace = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    while (!r.empty() && (isSpace(r.front()) || r.front() == '"' || r.front() == '\'')) {
        r.erase(r.begin());
    }
    while (!r.empty() && (isSpace(r.back()) || r.back() == '"' || r.back() == '\'')) {
        r.pop_back();
    }
    return r;
}
}  // namespace

SQLiteService::SQLiteService(const Json::Value& config) {
    // 读取 SQLite 节点配置, 不存在时使用默认值
    std::string rootDir;
    std::string defaultDbName;

    if (config.isMember("SQLite") && config["SQLite"].isObject()) {
        const Json::Value& node = config["SQLite"];
        if (node.isMember("root_dir") && node["root_dir"].isString()) {
            rootDir = stripQuotesAndSpaces(node["root_dir"].asString());
        }
        if (node.isMember("default_db_name") && node["default_db_name"].isString()) {
            defaultDbName = stripQuotesAndSpaces(node["default_db_name"].asString());
        }
    }

    if (rootDir.empty()) {
        rootDir = "sqlite_data";
    }
    if (defaultDbName.empty()) {
        defaultDbName = "default";
    }

    namespace fs = std::filesystem;
    fs::path rootPath(rootDir);
    if (!rootPath.is_absolute()) {
        rootPath = fs::absolute(rootPath);
    }
    std::error_code ec;
    if (!fs::exists(rootPath, ec)) {
        fs::create_directories(rootPath, ec);
        if (ec) {
            LOG_ERROR << "SQLiteService 创建根目录失败: " << rootPath.string()
                      << ", err=" << ec.message();
        }
    }

    _rootDir = rootPath.string();
    _defaultDbName = defaultDbName;

    LOG_INFO << "SQLiteService 初始化完成, root=" << _rootDir
             << ", default=" << _defaultDbName;
}

SQLiteService::~SQLiteService() {
    closeAll();
}

std::string SQLiteService::resolveDbPath(const std::string& logicalName) const {
    std::string name = logicalName.empty() ? _defaultDbName : logicalName;
    namespace fs = std::filesystem;
    fs::path p = fs::path(_rootDir) / (name + std::string(".sqlite"));
    return p.string();
}

SqliteConnectionPtr SQLiteService::getConnection(const std::string& logicalName) {
    std::string key = logicalName.empty() ? _defaultDbName : logicalName;

    std::lock_guard<std::mutex> lock(_poolMutex);
    auto it = _pool.find(key);
    if (it != _pool.end()) {
        return it->second;
    }

    SqliteConnectionPtr conn(new SqliteConnection());
    std::string path = resolveDbPath(key);
    if (!conn->open(path)) {
        LOG_ERROR << "SQLiteService 打开数据库失败: " << path
                  << ", err=" << conn->lastError();
        return SqliteConnectionPtr();
    }
    _pool[key] = conn;
    return conn;
}

SqliteConnectionPtr SQLiteService::getDefaultConnection() {
    return getConnection(_defaultDbName);
}

void SQLiteService::releaseConnection(const std::string& logicalName) {
    std::string key = logicalName.empty() ? _defaultDbName : logicalName;
    std::lock_guard<std::mutex> lock(_poolMutex);
    auto it = _pool.find(key);
    if (it != _pool.end()) {
        if (it->second) {
            it->second->close();
        }
        _pool.erase(it);
    }
}

void SQLiteService::closeAll() {
    std::lock_guard<std::mutex> lock(_poolMutex);
    for (auto& kv : _pool) {
        if (kv.second) {
            kv.second->close();
        }
    }
    _pool.clear();
}

bool SQLiteService::execute(const std::string& logicalName,
                            const std::string& sql,
                            const std::vector<SqliteValue>& params,
                            int64_t* affectedRows) {
    SqliteConnectionPtr conn = getConnection(logicalName);
    if (!conn) {
        return false;
    }
    return conn->execute(sql, params, affectedRows);
}

SqliteRecordsetPtr SQLiteService::query(const std::string& logicalName,
                                        const std::string& sql,
                                        const std::vector<SqliteValue>& params) {
    SqliteConnectionPtr conn = getConnection(logicalName);
    if (!conn) {
        return SqliteRecordsetPtr();
    }
    return conn->query(sql, params);
}

Json::Value SQLiteService::recordsetToJson(const SqliteRecordsetPtr& rs) const {
    Json::Value arr(Json::arrayValue);
    if (!rs) {
        return arr;
    }
    const std::vector<std::string>& names = rs->columnNames();
    for (int r = 0; r < rs->rowCount(); ++r) {
        Json::Value obj(Json::objectValue);
        for (size_t c = 0; c < names.size(); ++c) {
            const std::string& colName = names[c];
            const SqliteValue& v = rs->valueAt(r, colName);
            switch (v.type()) {
            case SqliteValue::vtNull:
                obj[colName] = Json::Value();
                break;
            case SqliteValue::vtInt:
                obj[colName] = static_cast<Json::Int64>(v.asInt());
                break;
            case SqliteValue::vtReal:
                obj[colName] = v.asReal();
                break;
            case SqliteValue::vtText:
                obj[colName] = v.asText();
                break;
            case SqliteValue::vtBlob:
                // blob 暂以长度字符串返回, 业务需要原始数据时可扩展为 base64
                obj[colName] = v.toString();
                break;
            }
        }
        arr.append(obj);
    }
    return arr;
}

drogon::Task<HttpResult> SQLiteService::executeAsync(std::string logicalName,
                                                    std::string sql,
                                                    std::vector<SqliteValue> params) {
    // 第一阶段直接同步执行, 等业务规模扩大或出现明显阻塞再下沉到线程池.
    // 协程语义保留, 调用方可以平滑切换为真正异步实现.
    HttpResult result;
    int64_t affected = 0;
    bool ok = false;
    std::string err;

    SqliteConnectionPtr conn = getConnection(logicalName);
    if (!conn) {
        err = std::string("failed to open sqlite connection: ") + logicalName;
    } else {
        ok = conn->execute(sql, params, &affected);
        if (!ok) {
            err = conn->lastError();
        }
    }

    if (ok) {
        result.code = 0;
        result.msg = "success";
        result.jsondata["affectedRows"] = static_cast<Json::Int64>(affected);
        result.jsondata["ok"] = true;
    } else {
        result.setResult(UEAdminAPI::ApiError_DatabaseError, err.empty() ? std::string("sqlite execute failed") : err);
        result.jsondata["ok"] = false;
    }
    co_return result;
}

drogon::Task<HttpResult> SQLiteService::queryAsync(std::string logicalName,
                                                  std::string sql,
                                                  std::vector<SqliteValue> params) {
    HttpResult result;
    SqliteRecordsetPtr rs;
    std::string err;

    SqliteConnectionPtr conn = getConnection(logicalName);
    if (!conn) {
        err = std::string("failed to open sqlite connection: ") + logicalName;
    } else {
        rs = conn->query(sql, params);
        if (!rs) {
            err = conn->lastError();
        }
    }

    if (rs) {
        result.code = 0;
        result.msg = "success";
        result.jsondata["rows"] = recordsetToJson(rs);
        result.jsondata["rowCount"] = rs->rowCount();
    } else {
        result.setResult(UEAdminAPI::ApiError_DatabaseError, err.empty() ? std::string("sqlite query failed") : err);
    }
    co_return result;
}

drogon::Task<HttpResult> SQLiteService::ping() {
    // 直接复用 queryAsync, 落到默认库, 跑一句 SELECT 1
    std::vector<SqliteValue> noParams;
    HttpResult r = co_await queryAsync(_defaultDbName, std::string("SELECT 1 AS ok;"), noParams);
    if (r.code == 0) {
        r.msg = "sqlite ok";
    }
    co_return r;
}

// ---- SQL RPC ----

int SQLiteService::countQuestionMarks(const std::string& sql) {
    // 简易版本: 跳过单引号字符串内的 ?, 便于捕获错误参数数量
    int count = 0;
    bool inQuote = false;
    for (size_t i = 0; i < sql.size(); ++i) {
        char c = sql[i];
        if (c == '\'') {
            // SQLite 允许 '' 表示单引号本身
            if (inQuote && i + 1 < sql.size() && sql[i + 1] == '\'') {
                ++i;
                continue;
            }
            inQuote = !inQuote;
            continue;
        }
        if (!inQuote && c == '?') {
            ++count;
        }
    }
    return count;
}

std::string SQLiteService::newTxToken() {
    // 生成 32 位十六进制字符串, 与 sqlite3 无关, 仅作为服务端 token
    static thread_local std::mt19937_64 rng(
        static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<uint64_t> dist;
    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << dist(rng)
        << std::setw(16) << std::setfill('0') << dist(rng);
    return oss.str();
}

drogon::Task<HttpResult> SQLiteService::handleQueryRpc(const SqliteRpcRequest& req) {
    HttpResult result;

    // 1. 基础字段校验
    if (req.sql.empty()) {
        result.setResult(UEAdminAPI::ApiError_SqliteRpc_MissingSql);
        co_return result;
    }
    if (req.logicalName.empty()) {
        result.setResult(UEAdminAPI::ApiError_SqliteRpc_LogicalNameMissing);
        co_return result;
    }

    // 2. 参数数量校验
    int qCount = countQuestionMarks(req.sql);
    if (static_cast<int>(req.params.size()) != qCount) {
        result.setResult(UEAdminAPI::ApiError_SqliteRpc_ParamMismatch,
                         std::string("SQL 需要 ") + std::to_string(qCount)
                             + " 个参数, 实际提供 " + std::to_string(req.params.size()));
        co_return result;
    }

    // 3. 如果指定了 txId, 检查它是否属于同一 logicalName
    if (!req.txId.empty()) {
        std::lock_guard<std::mutex> lock(_txMutex);
        auto it = _activeTx.find(req.txId);
        if (it == _activeTx.end()) {
            result.setResult(UEAdminAPI::ApiError_SqliteRpc_TxIdInvalid);
            co_return result;
        }
        if (it->second.logicalName != req.logicalName) {
            result.setResult(UEAdminAPI::ApiError_SqliteRpc_TxIdInvalid, "事务 token 与逻辑库不匹配");
            co_return result;
        }
    }

    // 4. 拼接可选的 LIMIT/OFFSET; 若 SQL 已有 LIMIT 由客户端负责, 服务端不再重复添加
    std::string sqlFinal = req.sql;
    if (req.limit >= 0) {
        // 仅在 SQL 中不含 LIMIT 时追加, 简单大小写不敏感检查
        std::string upper = sqlFinal;
        std::transform(upper.begin(), upper.end(), upper.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (upper.find(" LIMIT ") == std::string::npos) {
            sqlFinal += " LIMIT " + std::to_string(req.limit);
            if (req.offset > 0) {
                sqlFinal += " OFFSET " + std::to_string(req.offset);
            }
        }
    }

    // 5. 落到 queryAsync 执行
    HttpResult r = co_await queryAsync(req.logicalName, sqlFinal, req.params);

    if (r.code == 0 && !req.requestId.empty()) {
        r.jsondata["requestId"] = req.requestId;
    }
    co_return r;
}

drogon::Task<HttpResult> SQLiteService::handleExecuteRpc(const SqliteRpcRequest& req) {
    HttpResult result;

    if (req.sql.empty()) {
        result.setResult(UEAdminAPI::ApiError_SqliteRpc_MissingSql);
        co_return result;
    }
    if (req.logicalName.empty()) {
        result.setResult(UEAdminAPI::ApiError_SqliteRpc_LogicalNameMissing);
        co_return result;
    }
    if (req.readOnly) {
        result.setResult(UEAdminAPI::ApiError_InvalidOperation, "readOnly 请求不允许调用 exec 接口");
        co_return result;
    }

    int qCount = countQuestionMarks(req.sql);
    if (static_cast<int>(req.params.size()) != qCount) {
        result.setResult(UEAdminAPI::ApiError_SqliteRpc_ParamMismatch,
                         std::string("SQL 需要 ") + std::to_string(qCount)
                             + " 个参数, 实际提供 " + std::to_string(req.params.size()));
        co_return result;
    }

    if (!req.txId.empty()) {
        std::lock_guard<std::mutex> lock(_txMutex);
        auto it = _activeTx.find(req.txId);
        if (it == _activeTx.end()) {
            result.setResult(UEAdminAPI::ApiError_SqliteRpc_TxIdInvalid);
            co_return result;
        }
        if (it->second.logicalName != req.logicalName) {
            result.setResult(UEAdminAPI::ApiError_SqliteRpc_TxIdInvalid, "事务 token 与逻辑库不匹配");
            co_return result;
        }
    }

    HttpResult r = co_await executeAsync(req.logicalName, req.sql, req.params);

    if (r.code == 0 && !req.requestId.empty()) {
        r.jsondata["requestId"] = req.requestId;
    }
    co_return r;
}

drogon::Task<HttpResult> SQLiteService::beginTransaction(const std::string& logicalName) {
    HttpResult result;

    if (logicalName.empty()) {
        result.setResult(UEAdminAPI::ApiError_SqliteRpc_LogicalNameMissing);
        co_return result;
    }

    // 每个逻辑库同时只允许一个活动事务, 简化模型
    {
        std::lock_guard<std::mutex> lock(_txMutex);
        auto it = _logicalToTx.find(logicalName);
        if (it != _logicalToTx.end()) {
            result.setResult(UEAdminAPI::ApiError_SqliteRpc_TxAlreadyExists);
            result.jsondata["existingTxId"] = it->second;
            co_return result;
        }
    }

    SqliteConnectionPtr conn = getConnection(logicalName);
    if (!conn) {
        result.setResult(UEAdminAPI::ApiError_SqliteRpc_LogicalNameUnknown, logicalName);
        co_return result;
    }
    if (!conn->beginTransaction()) {
        result.setResult(UEAdminAPI::ApiError_DatabaseError, conn->lastError());
        co_return result;
    }

    std::string txId = newTxToken();
    {
        std::lock_guard<std::mutex> lock(_txMutex);
        TxInfo info;
        info.logicalName = logicalName;
        info.expiresAt = std::chrono::steady_clock::now() + std::chrono::seconds(_txTimeoutSec);
        _activeTx[txId] = info;
        _logicalToTx[logicalName] = txId;
    }

    result.code = 0;
    result.msg = "success";
    result.jsondata["txId"] = txId;
    result.jsondata["expiresInSec"] = _txTimeoutSec;
    co_return result;
}

drogon::Task<HttpResult> SQLiteService::commitTransaction(const std::string& txId) {
    HttpResult result;
    if (txId.empty()) {
        result.setResult(UEAdminAPI::ApiError_SqliteRpc_TxIdMissing);
        co_return result;
    }

    std::string logicalName;
    {
        std::lock_guard<std::mutex> lock(_txMutex);
        auto it = _activeTx.find(txId);
        if (it == _activeTx.end()) {
            result.setResult(UEAdminAPI::ApiError_SqliteRpc_TxIdInvalid);
            co_return result;
        }
        logicalName = it->second.logicalName;
        _activeTx.erase(it);
        _logicalToTx.erase(logicalName);
    }

    SqliteConnectionPtr conn = getConnection(logicalName);
    if (!conn) {
        result.setResult(UEAdminAPI::ApiError_SqliteRpc_LogicalNameUnknown, logicalName);
        co_return result;
    }
    if (!conn->commitTransaction()) {
        result.setResult(UEAdminAPI::ApiError_DatabaseError, conn->lastError());
        co_return result;
    }

    result.code = 0;
    result.msg = "success";
    result.jsondata["txId"] = txId;
    co_return result;
}

drogon::Task<HttpResult> SQLiteService::rollbackTransaction(const std::string& txId) {
    HttpResult result;
    if (txId.empty()) {
        result.setResult(UEAdminAPI::ApiError_SqliteRpc_TxIdMissing);
        co_return result;
    }

    std::string logicalName;
    {
        std::lock_guard<std::mutex> lock(_txMutex);
        auto it = _activeTx.find(txId);
        if (it == _activeTx.end()) {
            result.setResult(UEAdminAPI::ApiError_SqliteRpc_TxIdInvalid);
            co_return result;
        }
        logicalName = it->second.logicalName;
        _activeTx.erase(it);
        _logicalToTx.erase(logicalName);
    }

    SqliteConnectionPtr conn = getConnection(logicalName);
    if (!conn) {
        result.setResult(UEAdminAPI::ApiError_SqliteRpc_LogicalNameUnknown, logicalName);
        co_return result;
    }
    if (!conn->rollbackTransaction()) {
        result.setResult(UEAdminAPI::ApiError_DatabaseError, conn->lastError());
        co_return result;
    }

    result.code = 0;
    result.msg = "success";
    result.jsondata["txId"] = txId;
    co_return result;
}

drogon::Task<HttpResult> SQLiteService::resolveLogicalName(const std::string& scope,
                                                          const std::string& projectCode,
                                                          const std::string& templateKind,
                                                          const std::string& fileName) {
    HttpResult result;
    // 简化版本: 直接按 <scope>.<projectCode>.<templateKind>[.<fileName>] 拼装.
    // 未来接入 core.project_databases 表后可改为查表.
    if (scope.empty() || templateKind.empty()) {
        result.setResult(UEAdminAPI::ApiError_MissingRequiredArgs, "scope 和 templateKind 必填");
        co_return result;
    }

    std::string logicalName;
    if (scope == "global") {
        logicalName = "global." + templateKind;
        if (!fileName.empty()) {
            logicalName += "." + fileName;
        }
    } else if (scope == "project") {
        if (projectCode.empty()) {
            result.setResult(UEAdminAPI::ApiError_MissingRequiredArgs, "scope=project 时 projectCode 必填");
            co_return result;
        }
        logicalName = "project." + projectCode + "." + templateKind;
        if (!fileName.empty()) {
            logicalName += "." + fileName;
        }
    } else {
        result.setResult(UEAdminAPI::ApiError_InvalidOperation, "scope 仅支持 project 或 global");
        co_return result;
    }

    result.code = 0;
    result.msg = "success";
    result.jsondata["logicalName"] = logicalName;
    co_return result;
}

}  // namespace Services
}  // namespace UEAdminAPI

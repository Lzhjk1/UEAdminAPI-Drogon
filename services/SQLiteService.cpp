#include "SQLiteService.h"

#include "utils/ApiErrorCodes.h"
#include "utils/BackgroundExecutor.h"
#include "utils/ProcessRunner.h"
#include "AuditLogService.h"
#include "IdempotentService.h"

#include <drogon/orm/DbClient.h>
#include <drogon/orm/Mapper.h>
#include <trantor/utils/Logger.h>

#include "ProjectDatabases.h"
#include "Projects.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

std::string toLowerCopy(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return r;
}

std::vector<std::string> splitString(const std::string& s, char delim) {
    std::vector<std::string> parts;
    std::string current;
    std::istringstream iss(s);
    while (std::getline(iss, current, delim)) {
        parts.push_back(current);
    }
    return parts;
}

std::string joinParts(const std::vector<std::string>& parts, size_t beginIndex) {
    std::string result;
    for (size_t i = beginIndex; i < parts.size(); ++i) {
        if (!result.empty()) {
            result += ".";
        }
        result += parts[i];
    }
    return result;
}

bool fileExists(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    namespace fs = std::filesystem;
    std::error_code ec;
    return fs::exists(fs::path(path), ec) && !ec;
}

std::string fileNameFromPath(const std::string& path) {
    if (path.empty()) {
        return std::string();
    }
    namespace fs = std::filesystem;
    fs::path p(stripQuotesAndSpaces(path));
    return p.filename().string();
}

std::string tryConvertToSqliteSibling(const std::string& path) {
    if (path.empty()) {
        return std::string();
    }
    namespace fs = std::filesystem;
    fs::path p(stripQuotesAndSpaces(path));
    std::string ext = toLowerCopy(p.extension().string());
    if (ext == ".sqlite" || ext == ".db") {
        return p.string();
    }
    if (ext == ".mdb" || ext == ".accdb") {
        fs::path sqlitePath = p;
        sqlitePath.replace_extension(".sqlite");
        return sqlitePath.string();
    }
    return std::string();
}

std::string buildLogicalNameFromPieces(const std::string& scope,
                                       const std::string& projectCode,
                                       const std::string& templateKind,
                                       const std::string& fileName) {
    std::string scopeLower = toLowerCopy(scope);
    if (scopeLower.empty() || templateKind.empty()) {
        return std::string();
    }

    std::string logicalName;
    if (scopeLower == "global") {
        logicalName = "global." + templateKind;
    } else if (scopeLower == "project") {
        if (projectCode.empty()) {
            return std::string();
        }
        logicalName = "project." + projectCode + "." + templateKind;
    } else {
        return std::string();
    }

    if (!fileName.empty()) {
        logicalName += "." + fileName;
    }
    return logicalName;
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

    // 读取 MdbMigrator 节点配置 (迁移工具路径)
    if (config.isMember("MdbMigrator") && config["MdbMigrator"].isObject()) {
        const Json::Value& migNode = config["MdbMigrator"];
        if (migNode.isMember("path") && migNode["path"].isString()) {
            _migratorExePath = stripQuotesAndSpaces(migNode["path"].asString());
        }
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
             << ", default=" << _defaultDbName
             << ", migrator=" << (_migratorExePath.empty() ? "(未配置, 需通过 MDB_MIGRATOR_PATH 环境变量提供)" : _migratorExePath);

    startTxCleanupThread();
}

SQLiteService::~SQLiteService() {
    stopTxCleanupThread();
    closeAll();
}

void SQLiteService::startTxCleanupThread() {
    _txCleanupRunning = true;
    _txCleanupThread = std::thread([this]() {
        int sleepMs = (_txTimeoutSec / 2) * 1000;
        while (_txCleanupRunning) {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
            if (!_txCleanupRunning) break;

            // 1. 在锁中收集超时事务列表
            std::vector<std::pair<std::string, std::string>> expired;  // (txId, logicalName)
            {
                auto now = std::chrono::steady_clock::now();
                std::lock_guard<std::mutex> lock(_txMutex);
                for (auto it = _activeTx.begin(); it != _activeTx.end(); ) {
                    if (it->second.expiresAt <= now) {
                        expired.push_back({it->first, it->second.logicalName});
                        _logicalToTx.erase(it->second.logicalName);
                        it = _activeTx.erase(it);
                    } else {
                        ++it;
                    }
                }
            }

            // 2. 锁外逐个执行 ROLLBACK
            for (auto& entry : expired) {
                SqliteConnectionPtr conn = getConnection(entry.second);
                if (conn) {
                    std::mutex* connMtx = getConnMutex(entry.second);
                    if (connMtx) {
                        std::lock_guard<std::mutex> connLock(*connMtx);
                        conn->rollbackTransaction();
                        LOG_WARN << "TxCleanup: 已回滚超时事务, txId=" << entry.first
                                 << ", logicalName=" << entry.second;
                    }
                } else {
                    LOG_ERROR << "TxCleanup: 无法获取连接, logicalName=" << entry.second;
                }
            }
        }
    });
}

void SQLiteService::stopTxCleanupThread() {
    _txCleanupRunning = false;
    if (_txCleanupThread.joinable()) {
        _txCleanupThread.join();
    }
}

std::string SQLiteService::buildFallbackLogicalName(const std::string& scope,
                                                    const std::string& projectCode,
                                                    const std::string& templateKind,
                                                    const std::string& fileName) {
    return buildLogicalNameFromPieces(scope, projectCode, templateKind, fileName);
}

bool SQLiteService::lookupRouteByRequest(const std::string& scope,
                                         const std::string& projectCode,
                                         const std::string& templateKind,
                                         const std::string& fileName,
                                         RouteInfo* info) const {
    if (info == NULL) {
        return false;
    }

    RouteInfo route;
    route.scope = toLowerCopy(scope);
    route.projectCode = projectCode;
    route.templateKind = templateKind;
    route.fileName = fileName;

    if (route.scope.empty() || route.templateKind.empty()) {
        return false;
    }

    try {
        drogon::orm::DbClientPtr dbClientPtr = drogon::app().getDbClient();
        if (!dbClientPtr) {
            return false;
        }

        using drogon::orm::Criteria;
        using drogon::orm::CompareOperator;
        using drogon::orm::Mapper;
        using PD = drogon_model::UELocalDB::core::ProjectDatabases;
        using Proj = drogon_model::UELocalDB::core::Projects;

        Mapper<PD> pdMapper(dbClientPtr);
        std::vector<PD> rows;

        // 只查启用的记录
        Criteria baseCriteria =
            Criteria(PD::Cols::_scope_type, CompareOperator::EQ, route.scope) &&
            Criteria(PD::Cols::_template_kind, CompareOperator::EQ, route.templateKind) &&
            Criteria(PD::Cols::_is_enabled, CompareOperator::EQ, true);

        if (route.scope == "project") {
            if (route.projectCode.empty()) {
                return false;
            }

            // 先按 code 查 core.projects, 拿到 project_id
            Mapper<Proj> projMapper(dbClientPtr);
            std::vector<Proj> projects = projMapper.findBy(
                Criteria(Proj::Cols::_code, CompareOperator::EQ, route.projectCode));
            if (projects.empty()) {
                return false;
            }
            int64_t projectId = projects[0].getValueOfId();

            rows = pdMapper.findBy(
                baseCriteria &&
                Criteria(PD::Cols::_project_id, CompareOperator::EQ, projectId));
        } else if (route.scope == "global") {
            rows = pdMapper.findBy(
                baseCriteria &&
                Criteria(PD::Cols::_project_id, CompareOperator::IsNull));
        } else {
            return false;
        }

        // 若提供 fileName, 优先匹配 source_name
        if (!route.fileName.empty()) {
            std::vector<PD> filtered;
            for (size_t i = 0; i < rows.size(); ++i) {
                const std::shared_ptr<std::string>& sn = rows[i].getSourceName();
                if (sn && *sn == route.fileName) {
                    filtered.push_back(rows[i]);
                    break;
                }
            }
            if (!filtered.empty()) {
                rows.swap(filtered);
            }
        }

        if (rows.empty()) {
            return false;
        }

        const PD& pd = rows[0];
        route.found = true;
        route.scope = pd.getValueOfScopeType();
        route.templateKind = pd.getValueOfTemplateKind();

        // 反查 project code (公共库 project_id 为空, 留空)
        const std::shared_ptr<int64_t>& pid = pd.getProjectId();
        if (pid) {
            Mapper<Proj> projMapper(dbClientPtr);
            try {
                Proj proj = projMapper.findByPrimaryKey(*pid);
                route.projectCode = proj.getValueOfCode();
            } catch (const std::exception& e) {
                LOG_WARN << "SQLiteService 反查 core.projects 失败: " << e.what();
            }
        }

        const std::shared_ptr<std::string>& sn = pd.getSourceName();
        if (sn) {
            route.sourceName = *sn;
        }
        const std::shared_ptr<std::string>& sp = pd.getSourcePath();
        if (sp) {
            route.sourcePath = stripQuotesAndSpaces(*sp);
        }

        if (route.fileName.empty()) {
            if (!route.sourceName.empty()) {
                route.fileName = route.sourceName;
            } else {
                route.fileName = fileNameFromPath(route.sourcePath);
            }
        }

        route.logicalName = buildLogicalNameFromPieces(
            route.scope, route.projectCode, route.templateKind, route.fileName);

        if (route.logicalName.empty()) {
            return false;
        }

        *info = route;
        return true;
    } catch (const std::exception& e) {
        LOG_WARN << "SQLiteService 查询 core.project_databases 失败: " << e.what();
        return false;
    }
}

bool SQLiteService::lookupRouteByLogicalName(const std::string& logicalName, RouteInfo* info) const {
    if (info == NULL) {
        return false;
    }

    std::vector<std::string> parts = splitString(logicalName, '.');
    if (parts.size() < 2) {
        return false;
    }

    std::string scope = toLowerCopy(parts[0]);
    std::string projectCode;
    std::string templateKind;
    std::string fileName;

    if (scope == "global") {
        templateKind = parts[1];
        if (parts.size() > 2) {
            fileName = joinParts(parts, 2);
        }
    } else if (scope == "project") {
        if (parts.size() < 3) {
            return false;
        }
        projectCode = parts[1];
        templateKind = parts[2];
        if (parts.size() > 3) {
            fileName = joinParts(parts, 3);
        }
    } else {
        return false;
    }

    return lookupRouteByRequest(scope, projectCode, templateKind, fileName, info);
}

std::string SQLiteService::resolveDbPath(const std::string& logicalName) const {
    std::string name = logicalName.empty() ? _defaultDbName : logicalName;
    RouteInfo route;
    if (lookupRouteByLogicalName(name, &route)) {
        if (!route.sourcePath.empty()) {
            if (fileExists(route.sourcePath)) {
                return route.sourcePath;
            }

            std::string sqliteSibling = tryConvertToSqliteSibling(route.sourcePath);
            if (!sqliteSibling.empty() && fileExists(sqliteSibling)) {
                LOG_INFO << "SQLiteService 使用元数据同名 .sqlite 文件: " << sqliteSibling;
                return sqliteSibling;
            }

            std::string ext = toLowerCopy(std::filesystem::path(route.sourcePath).extension().string());
            if (ext == ".sqlite" || ext == ".db") {
                return route.sourcePath;
            }
        }
    }

    namespace fs = std::filesystem;
    fs::path p = fs::path(_rootDir) / (name + std::string(".sqlite"));
    return p.string();
}

std::mutex* SQLiteService::getConnMutex(const std::string& logicalName) const {
    std::string key = logicalName.empty() ? _defaultDbName : logicalName;
    std::lock_guard<std::mutex> lock(_poolMutex);
    auto it = _connMutexes.find(key);
    if (it != _connMutexes.end()) {
        return it->second.get();
    }
    return nullptr;
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
    _connMutexes[key] = std::unique_ptr<std::mutex>(new std::mutex());
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
    _connMutexes.erase(key);
}

void SQLiteService::closeAll() {
    std::lock_guard<std::mutex> lock(_poolMutex);
    for (auto& kv : _pool) {
        if (kv.second) {
            kv.second->close();
        }
    }
    _pool.clear();
    _connMutexes.clear();
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
                                                    std::vector<SqliteValue> params,
                                                    int32_t timeoutMs) {
    // 通过 BackgroundAwaiter 将阻塞 SQLite 操作投递到后台线程,
    // 避免阻塞 Drogon IO 线程.
    HttpResult result;
    int64_t affected = 0;
    bool ok = false;
    std::string err;

    SqliteConnectionPtr conn = getConnection(logicalName);
    if (!conn) {
        err = std::string("failed to open sqlite connection: ") + logicalName;
    } else {
        std::mutex* connMtx = getConnMutex(logicalName);
        ok = co_await runOnBackground(
            [conn, &sql, &params, &affected, timeoutMs]() -> bool {
                return conn->execute(sql, params, &affected, timeoutMs);
            },
            connMtx);
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
                                                  std::vector<SqliteValue> params,
                                                  int32_t timeoutMs) {
    HttpResult result;
    SqliteRecordsetPtr rs;
    std::string err;

    SqliteConnectionPtr conn = getConnection(logicalName);
    if (!conn) {
        err = std::string("failed to open sqlite connection: ") + logicalName;
    } else {
        std::mutex* connMtx = getConnMutex(logicalName);
        rs = co_await runOnBackground(
            [conn, &sql, &params, timeoutMs]() -> SqliteRecordsetPtr {
                return conn->query(sql, params, timeoutMs);
            },
            connMtx);
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

    // 3. 幂等去重: 命中则直接返回上次结果
    if (!req.requestId.empty()) {
        auto cached = IdempotentService::get(req.requestId);
        if (cached) {
            Json::CharReaderBuilder builder;
            std::string errs;
            std::istringstream iss(*cached);
            Json::Value root;
            if (Json::parseFromStream(builder, iss, &root, &errs)) {
                HttpResult r;
                r.code = root.get("code", 0).asInt();
                r.msg = root.get("msg", "success").asString();
                if (root.isMember("data") && !root["data"].isNull()) {
                    r.jsondata = root["data"];
                }
                co_return r;
            }
            LOG_WARN << "Idempotent cache 解析失败, requestId=" << req.requestId << ", err=" << errs;
        }
    }

    // 4. 如果指定了 txId, 检查它是否属于同一 logicalName
    if (!req.txId.empty()) {
        std::lock_guard<std::mutex> lock(_txMutex);
        auto it = _activeTx.find(req.txId);
        if (it == _activeTx.end()) {
            result.setResult(UEAdminAPI::ApiError_SqliteRpc_TxIdInvalid);
            co_return result;
        }
        if (it->second.expiresAt <= std::chrono::steady_clock::now()) {
            result.setResult(UEAdminAPI::ApiError_SqliteRpc_TxIdInvalid, "事务已超时, 请重新开始事务");
            co_return result;
        }
        if (it->second.logicalName != req.logicalName) {
            result.setResult(UEAdminAPI::ApiError_SqliteRpc_TxIdInvalid, "事务 token 与逻辑库不匹配");
            co_return result;
        }
    }

    // 5. 拼接可选的 LIMIT/OFFSET; 若 SQL 已有 LIMIT 由客户端负责, 服务端不再重复添加
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

    // 6. 落到 queryAsync 执行 (传入超时参数)
    HttpResult r = co_await queryAsync(req.logicalName, sqlFinal, req.params, req.timeoutMs);

    if (r.code == 0 && !req.requestId.empty()) {
        r.jsondata["requestId"] = req.requestId;
    }

    // 6. 写幂等缓存 (仅成功结果)
    if (r.code == 0 && !req.requestId.empty()) {
        IdempotentService::put(req.requestId, r.toJsonString());
    }

    // 7. 审计日志 (异步, 不阻塞响应; 以 requestId 作为幂等键)
    {
        Json::Value detail;
        detail["logicalName"] = req.logicalName;
        detail["sql"] = req.sql;
        detail["txId"] = req.txId;
        detail["readOnly"] = req.readOnly;
        detail["result_code"] = r.code;

        AuditLogService::log(
            req.requestId,
            "sqlite.query",
            std::to_string(r.code),
            "sqlite_db",
            req.logicalName,
            detail.toStyledString(),
            req.userId);
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

    // 幂等去重: 命中则直接返回上次结果
    if (!req.requestId.empty()) {
        auto cached = IdempotentService::get(req.requestId);
        if (cached) {
            Json::CharReaderBuilder builder;
            std::string errs;
            std::istringstream iss(*cached);
            Json::Value root;
            if (Json::parseFromStream(builder, iss, &root, &errs)) {
                HttpResult r;
                r.code = root.get("code", 0).asInt();
                r.msg = root.get("msg", "success").asString();
                if (root.isMember("data") && !root["data"].isNull()) {
                    r.jsondata = root["data"];
                }
                co_return r;
            }
            LOG_WARN << "Idempotent cache 解析失败, requestId=" << req.requestId << ", err=" << errs;
        }
    }

    if (!req.txId.empty()) {
        std::lock_guard<std::mutex> lock(_txMutex);
        auto it = _activeTx.find(req.txId);
        if (it == _activeTx.end()) {
            result.setResult(UEAdminAPI::ApiError_SqliteRpc_TxIdInvalid);
            co_return result;
        }
        if (it->second.expiresAt <= std::chrono::steady_clock::now()) {
            result.setResult(UEAdminAPI::ApiError_SqliteRpc_TxIdInvalid, "事务已超时, 请重新开始事务");
            co_return result;
        }
        if (it->second.logicalName != req.logicalName) {
            result.setResult(UEAdminAPI::ApiError_SqliteRpc_TxIdInvalid, "事务 token 与逻辑库不匹配");
            co_return result;
        }
    }

    HttpResult r = co_await executeAsync(req.logicalName, req.sql, req.params, req.timeoutMs);

    if (r.code == 0 && !req.requestId.empty()) {
        r.jsondata["requestId"] = req.requestId;
    }

    // 写幂等缓存 (仅成功结果)
    if (r.code == 0 && !req.requestId.empty()) {
        IdempotentService::put(req.requestId, r.toJsonString());
    }

    // 审计日志 (以 requestId 作为幂等键)
    {
        Json::Value detail;
        detail["logicalName"] = req.logicalName;
        detail["sql"] = req.sql;
        detail["txId"] = req.txId;
        detail["result_code"] = r.code;
        if (r.jsondata.isMember("affectedRows")) {
            detail["affectedRows"] = r.jsondata["affectedRows"];
        }

        AuditLogService::log(
            req.requestId,
            "sqlite.exec",
            std::to_string(r.code),
            "sqlite_db",
            req.logicalName,
            detail.toStyledString(),
            req.userId);
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
        if (it->second.expiresAt <= std::chrono::steady_clock::now()) {
            result.setResult(UEAdminAPI::ApiError_SqliteRpc_TxIdInvalid, "事务已超时, 请重新开始事务");
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
        if (it->second.expiresAt <= std::chrono::steady_clock::now()) {
            result.setResult(UEAdminAPI::ApiError_SqliteRpc_TxIdInvalid, "事务已超时, 请重新开始事务");
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
    if (scope.empty() || templateKind.empty()) {
        result.setResult(UEAdminAPI::ApiError_MissingRequiredArgs, "scope 和 templateKind 必填");
        co_return result;
    }

    std::string scopeLower = toLowerCopy(scope);
    if (scopeLower == "project") {
        if (projectCode.empty()) {
            result.setResult(UEAdminAPI::ApiError_MissingRequiredArgs, "scope=project 时 projectCode 必填");
            co_return result;
        }
    } else if (scopeLower != "global") {
        result.setResult(UEAdminAPI::ApiError_InvalidOperation, "scope 仅支持 project 或 global");
        co_return result;
    }

    RouteInfo route;
    bool found = lookupRouteByRequest(scopeLower, projectCode, templateKind, fileName, &route);

    std::string logicalName = buildFallbackLogicalName(scopeLower, projectCode, templateKind, fileName);
    if (found && !route.logicalName.empty()) {
        logicalName = route.logicalName;
    }

    result.code = 0;
    result.msg = "success";
    result.jsondata["logicalName"] = logicalName;
    result.jsondata["resolvedByMetadata"] = found;
    result.jsondata["routeSource"] = found ? "core.project_databases" : "fallback";
    if (found) {
        result.jsondata["scope"] = route.scope;
        result.jsondata["templateKind"] = route.templateKind;
        if (!route.projectCode.empty()) {
            result.jsondata["projectCode"] = route.projectCode;
        }
        if (!route.sourceName.empty()) {
            result.jsondata["sourceName"] = route.sourceName;
        }
        if (!route.sourcePath.empty()) {
            result.jsondata["sourcePath"] = route.sourcePath;
        }
    }
    co_return result;
}

// ---- Access 上传与迁移 ----

// base64 解码
static std::string base64Decode(const std::string& encoded) {
    static const std::string base64Chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string decoded;
    int val = 0, valb = -8;
    for (char c : encoded) {
        if (c == '=') break;
        size_t pos = base64Chars.find(c);
        if (pos == std::string::npos) continue;
        val = (val << 6) | static_cast<int>(pos);
        valb += 6;
        if (valb >= 0) {
            decoded.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return decoded;
}

// 从 .mdb 文件名推断 templateKind (如 Admin.mdb -> admin, Design.mdb -> desi)
static std::string inferTemplateKind(const std::string& mdbFileName) {
    // 去掉路径, 只留文件名
    size_t lastSep = mdbFileName.find_last_of("\\/");
    std::string name = (lastSep != std::string::npos)
                           ? mdbFileName.substr(lastSep + 1)
                           : mdbFileName;

    // 去掉 .mdb 扩展名
    size_t dotPos = name.rfind('.');
    if (dotPos != std::string::npos) {
        name = name.substr(0, dotPos);
    }

    // 转小写
    std::string lower = toLowerCopy(name);

    // 已知映射 (与 core.project_databases 现有数据一致)
    if (lower == "admin") return "admin";
    if (lower == "design") return "desi";
    if (lower == "catalog") return "cata";
    if (lower == "specmanager") return "spec_manager";

    // 未知则用小写文件名
    return lower;
}

drogon::Task<HttpResult> SQLiteService::uploadMdb(
    const std::string& mdbFileName,
    const std::string& base64Data,
    const std::string& scope,
    const std::string& projectCode) {

    HttpResult result;

    if (mdbFileName.empty() || base64Data.empty()) {
        result.setResult(UEAdminAPI::ApiError_MissingRequiredArgs,
                         "mdbFileName 和 base64Data 必填");
        co_return result;
    }

    std::string scopeLower = toLowerCopy(scope);
    if (scopeLower.empty()) {
        scopeLower = "global";
    }

    if (scopeLower == "project" && projectCode.empty()) {
        result.setResult(UEAdminAPI::ApiError_MissingRequiredArgs,
                         "scope=project 时 projectCode 必填");
        co_return result;
    }

    if (scopeLower != "global" && scopeLower != "project") {
        result.setResult(UEAdminAPI::ApiError_InvalidOperation,
                         "scope 仅支持 project 或 global");
        co_return result;
    }

    // 1. 推断 templateKind
    std::string templateKind = inferTemplateKind(mdbFileName);
    std::string logicalName = buildLogicalNameFromPieces(scopeLower, projectCode, templateKind, "");
    if (logicalName.empty()) {
        result.setResult(UEAdminAPI::ApiError_InvalidOperation,
                         "无法构造 logicalName");
        co_return result;
    }

    // 2. 用 BackgroundAwaiter 在后台线程执行: base64 解码 + 保存临时 mdb + 调用迁移工具
    std::string rootDir = _rootDir;
    std::string mdbFileNameCopy = mdbFileName;

    struct MigrateResult {
        bool ok = false;
        std::string errMsg;
        std::string sqlitePath;
    };

    MigrateResult migrateRes = co_await runOnBackground([&]() -> MigrateResult {
        MigrateResult res;

        // 2a. base64 解码
        std::string mdbBytes = base64Decode(base64Data);
        if (mdbBytes.empty()) {
            res.errMsg = "base64 解码后为空";
            return res;
        }

        // 2b. 保存临时 .mdb 文件
        namespace fs = std::filesystem;
        fs::path tempDir = fs::temp_directory_path();
        fs::path mdbPath = tempDir / ("upload_" + std::to_string(std::time(nullptr)) + ".mdb");
        std::ofstream ofs(mdbPath, std::ios::binary);
        if (!ofs.is_open()) {
            res.errMsg = "无法创建临时文件: " + mdbPath.string();
            return res;
        }
        ofs.write(mdbBytes.data(), static_cast<std::streamsize>(mdbBytes.size()));
        ofs.close();

        // 2c. 构造输出 SQLite 路径
        fs::path sqliteDir = fs::path(rootDir);
        fs::path sqlitePath = sqliteDir / (logicalName + ".sqlite");

        // 2d. 调用 MdbToSqliteMigrator.exe (通过 utils::runProcess 跨平台启动)
        // 迁移工具路径: 优先环境变量 MDB_MIGRATOR_PATH 覆盖, 其次 config MdbMigrator.path, 均未设置则报错
        const char* migratorEnv = std::getenv("MDB_MIGRATOR_PATH");
        std::string migratorExe = migratorEnv ? std::string(migratorEnv) : _migratorExePath;
        if (migratorExe.empty()) {
            res.errMsg = "未配置迁移工具路径, 请在 config.yaml 的 custom_config.MdbMigrator.path 设置, 或设置环境变量 MDB_MIGRATOR_PATH";
            return res;
        }

        // 通过 utils::runProcess 启动迁移工具 (平台无关, Windows 内部使用 CreateProcessW)
        std::string cmd = "\"" + migratorExe + "\" \"" +
                          mdbPath.string() + "\" \"" +
                          sqlitePath.string() + "\"";

        LOG_INFO << "uploadMdb: 调用迁移工具: " << cmd;

        // 通过平台无关的 runProcess 启动迁移工具, 等待完成
        constexpr uint32_t kMigratorTimeoutMs = 120000;  // 迁移工具硬超时 120s
        auto pr = utils::runProcess(cmd, kMigratorTimeoutMs);
        if (!pr.started) {
            res.errMsg = "启动迁移工具失败: " + pr.errMsg;
            std::error_code ec;
            fs::remove(mdbPath, ec);
            return res;
        }
        if (pr.timedOut) {
            res.errMsg = "迁移工具执行超时(" + std::to_string(kMigratorTimeoutMs / 1000) + "s), 已强制终止";
            std::error_code ec;
            fs::remove(mdbPath, ec);
            return res;
        }
        if (pr.exitCode != 0) {
            res.errMsg = "迁移工具执行失败, exitCode=" + std::to_string(pr.exitCode);
            std::error_code ec;
            fs::remove(mdbPath, ec);
            return res;
        }

        // 2e. 检查输出文件 (存在且非空)
        std::error_code sizeEc;
        bool exists = fs::exists(sqlitePath);
        uintmax_t fileSize = exists ? fs::file_size(sqlitePath, sizeEc) : 0;
        if (sizeEc) fileSize = 0;  // file_size 出错视为无效
        if (!exists || fileSize == 0) {
            res.errMsg = "迁移后 SQLite 文件不存在或为空: " + sqlitePath.string()
                       + (exists ? " (size=0)" : " (not found)");
            std::error_code ec;
            fs::remove(mdbPath, ec);
            return res;
        }

        // 2f. 清理临时 .mdb
        std::error_code ec;
        fs::remove(mdbPath, ec);

        res.ok = true;
        res.sqlitePath = sqlitePath.string();
        return res;
    });

    if (!migrateRes.ok) {
        result.setResult(UEAdminAPI::ApiError_DatabaseError, migrateRes.errMsg);
        co_return result;
    }

    // 3. 注册到 core.project_databases (用 ORM)
    try {
        auto dbClientPtr = drogon::app().getDbClient();
        if (!dbClientPtr) {
            result.setResult(UEAdminAPI::ApiError_DatabaseError, "DbClient 为空");
            co_return result;
        }

        using drogon::orm::Criteria;
        using drogon::orm::CompareOperator;
        using drogon::orm::Mapper;
        using PD = drogon_model::UELocalDB::core::ProjectDatabases;
        using Proj = drogon_model::UELocalDB::core::Projects;

        Mapper<PD> mapper(dbClientPtr);

        // 先查是否已存在同 scope+templateKind 的路由 (global 唯一, project 按 projectCode 唯一)
        Criteria scopeCrit(PD::Cols::_scope_type, CompareOperator::EQ, scopeLower);
        Criteria kindCrit(PD::Cols::_template_kind, CompareOperator::EQ, templateKind);
        auto existing = mapper.findBy(scopeCrit && kindCrit);

        if (scopeLower == "global") {
            // global: 先删后插 (唯一约束 WHERE scope_type='global')
            for (auto& row : existing) {
                mapper.deleteByPrimaryKey(row.getValueOfId());
            }
        } else {
            // project: 按 projectCode 删后插
            // 需要先查 project_id
            Mapper<Proj> projMapper(dbClientPtr);
            auto projs = projMapper.findBy(
                Criteria(Proj::Cols::_code, CompareOperator::EQ, projectCode));
            int64_t projectId = 0;
            if (!projs.empty()) {
                projectId = projs[0].getValueOfId();
            }

            if (projectId > 0) {
                for (auto& row : existing) {
                    if (row.getValueOfProjectId() == projectId) {
                        mapper.deleteByPrimaryKey(row.getValueOfId());
                    }
                }
            }
        }

        // 插入新路由
        PD newRow;
        newRow.setScopeType(scopeLower);
        newRow.setTemplateKind(templateKind);
        newRow.setSourceName(mdbFileName);
        newRow.setSourcePath(migrateRes.sqlitePath);
        newRow.setIsEnabled(true);
        newRow.setDbVersion("1.0");

        if (scopeLower == "project") {
            Mapper<Proj> projMapper(dbClientPtr);
            auto projs = projMapper.findBy(
                Criteria(Proj::Cols::_code, CompareOperator::EQ, projectCode));
            if (!projs.empty()) {
                newRow.setProjectId(projs[0].getValueOfId());
            }
        }

        mapper.insert(newRow);

        LOG_INFO << "uploadMdb: 路由已注册, logicalName=" << logicalName
                 << ", sqlitePath=" << migrateRes.sqlitePath;

    } catch (const std::exception& e) {
        result.setResult(UEAdminAPI::ApiError_DatabaseError,
                         std::string("注册路由失败: ") + e.what());
        co_return result;
    }

    // 4. 返回 logicalName
    result.code = 0;
    result.msg = "success";
    result.jsondata["logicalName"] = logicalName;
    result.jsondata["templateKind"] = templateKind;
    result.jsondata["scope"] = scopeLower;
    result.jsondata["sqlitePath"] = migrateRes.sqlitePath;
    result.jsondata["tableCount"] = 0;
    result.jsondata["totalRows"] = 0;

    co_return result;
}

}  // namespace Services
}  // namespace UEAdminAPI

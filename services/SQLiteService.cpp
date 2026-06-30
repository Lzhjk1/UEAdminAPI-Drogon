#include "SQLiteService.h"

#include <trantor/utils/Logger.h>

#include <algorithm>
#include <cctype>
#include <filesystem>

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

}  // namespace Services
}  // namespace UEAdminAPI

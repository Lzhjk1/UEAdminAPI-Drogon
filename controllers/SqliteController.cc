#include "SqliteController.h"

#include "services/SQLiteService.h"
#include "utils/ApiErrorCodes.h"
#include "utils/HttpResult.h"

#include <trantor/utils/Logger.h>

#include <string>
#include <vector>

using namespace UEAdminAPI;
using namespace UEAdminAPI::SQLite;
using namespace UEAdminAPI::Services;
using namespace UEAdminAPI::utils;

namespace {

// 生成统一的 JSON 响应
HttpResponsePtr makeJsonResp(const HttpResult& result) {
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    resp->setContentTypeCode(CT_APPLICATION_JSON);
    resp->setBody(result.toJsonString());
    return resp;
}

// 从 JSON body 中读取一个字段, 缺失返回空字符串
std::string readStringField(const Json::Value& root, const char* key) {
    if (!root.isObject()) {
        return std::string();
    }
    if (!root.isMember(key)) {
        return std::string();
    }
    const Json::Value& v = root[key];
    if (v.isString()) {
        return v.asString();
    }
    if (v.isIntegral()) {
        return std::to_string(v.asInt64());
    }
    return std::string();
}

// 从 params JSON 数组解析为 vector<SqliteValue>
// 返回 false 时说明有元素格式非法, errMsg 内含定位信息
bool parseParams(const Json::Value& arr,
                 std::vector<SqliteValue>& out,
                 std::string& errMsg) {
    if (arr.isNull()) {
        // 不带 params 视为空数组
        return true;
    }
    if (!arr.isArray()) {
        errMsg = "params 必须是数组";
        return false;
    }
    out.reserve(arr.size());
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i) {
        const Json::Value& item = arr[i];
        if (!item.isObject()) {
            errMsg = std::string("params[") + std::to_string(i) + "] 必须是对象";
            return false;
        }
        std::string type = item.isMember("type") && item["type"].isString()
                               ? item["type"].asString()
                               : std::string("text");
        const Json::Value& val = item.isMember("value") ? item["value"] : Json::Value();

        if (type == "null") {
            out.push_back(SqliteValue::FromNull());
        } else if (type == "int") {
            if (val.isNull()) {
                out.push_back(SqliteValue::FromNull());
            } else if (val.isIntegral()) {
                out.push_back(SqliteValue::FromInt(val.asInt64()));
            } else if (val.isString()) {
                try {
                    out.push_back(SqliteValue::FromInt(std::stoll(val.asString())));
                } catch (...) {
                    errMsg = std::string("params[") + std::to_string(i) + "] 无法解析为整数";
                    return false;
                }
            } else {
                errMsg = std::string("params[") + std::to_string(i) + "] type=int 但 value 类型不匹配";
                return false;
            }
        } else if (type == "real") {
            if (val.isNull()) {
                out.push_back(SqliteValue::FromNull());
            } else if (val.isNumeric()) {
                out.push_back(SqliteValue::FromReal(val.asDouble()));
            } else if (val.isString()) {
                try {
                    out.push_back(SqliteValue::FromReal(std::stod(val.asString())));
                } catch (...) {
                    errMsg = std::string("params[") + std::to_string(i) + "] 无法解析为浮点数";
                    return false;
                }
            } else {
                errMsg = std::string("params[") + std::to_string(i) + "] type=real 但 value 类型不匹配";
                return false;
            }
        } else if (type == "text") {
            if (val.isNull()) {
                out.push_back(SqliteValue::FromNull());
            } else if (val.isString()) {
                out.push_back(SqliteValue::FromText(val.asString()));
            } else if (val.isIntegral()) {
                out.push_back(SqliteValue::FromText(std::to_string(val.asInt64())));
            } else if (val.isNumeric()) {
                out.push_back(SqliteValue::FromText(std::to_string(val.asDouble())));
            } else if (val.isBool()) {
                out.push_back(SqliteValue::FromText(val.asBool() ? "1" : "0"));
            } else {
                errMsg = std::string("params[") + std::to_string(i) + "] type=text 但 value 类型不支持";
                return false;
            }
        } else if (type == "blob") {
            // 简版: 仅支持字符串形式 (客户端未来可传 base64), 这里当作原始字节
            if (val.isString()) {
                const std::string& s = val.asString();
                std::vector<uint8_t> bytes(s.begin(), s.end());
                out.push_back(SqliteValue::FromBlob(std::move(bytes)));
            } else if (val.isNull()) {
                out.push_back(SqliteValue::FromNull());
            } else {
                errMsg = std::string("params[") + std::to_string(i) + "] type=blob 需要 value 为字符串";
                return false;
            }
        } else {
            errMsg = std::string("params[") + std::to_string(i) + "] 未知 type: " + type;
            return false;
        }
    }
    return true;
}

// 从 body 中解析成 SqliteRpcRequest
bool buildRpcRequest(const Json::Value& root,
                     SqliteRpcRequest& out,
                     HttpResult& errResult) {
    if (!root.isObject()) {
        errResult.setResult(ApiError_InvalidJsonFormat);
        return false;
    }

    out.sql = readStringField(root, "sql");
    out.txId = readStringField(root, "txId");
    out.requestId = readStringField(root, "requestId");

    // logicalName 首选顶层 logicalName, 兼容 db.logicalName
    out.logicalName = readStringField(root, "logicalName");
    if (out.logicalName.empty() && root.isMember("db") && root["db"].isObject()) {
        out.logicalName = readStringField(root["db"], "logicalName");
    }

    // 数字型可选字段
    if (root.isMember("limit") && root["limit"].isIntegral()) {
        out.limit = root["limit"].asInt64();
    }
    if (root.isMember("offset") && root["offset"].isIntegral()) {
        out.offset = root["offset"].asInt64();
    }
    if (root.isMember("timeoutMs") && root["timeoutMs"].isIntegral()) {
        out.timeoutMs = root["timeoutMs"].asInt();
    }
    if (root.isMember("readOnly") && root["readOnly"].isBool()) {
        out.readOnly = root["readOnly"].asBool();
    }
    if (root.isMember("wantColumns") && root["wantColumns"].isBool()) {
        out.wantColumns = root["wantColumns"].asBool();
    }

    // params
    std::string paramErr;
    if (!parseParams(root["params"], out.params, paramErr)) {
        errResult.setResult(ApiError_SqliteRpc_InvalidParamType, paramErr);
        return false;
    }

    return true;
}

}  // namespace

Task<HttpResponsePtr> SqliteController::Query(HttpRequestPtr req) {
    auto _sqliteService = SQLiteService::Instance();

    HttpResult result;
    auto reqJson = req->getJsonObject();
    if (!reqJson) {
        result.setResult(ApiError_InvalidJsonFormat);
        co_return makeJsonResp(result);
    }

    SqliteRpcRequest rpc;
    if (!buildRpcRequest(*reqJson, rpc, result)) {
        co_return makeJsonResp(result);
    }

    result = co_await _sqliteService->handleQueryRpc(rpc);
    co_return makeJsonResp(result);
}

Task<HttpResponsePtr> SqliteController::Execute(HttpRequestPtr req) {
    auto _sqliteService = SQLiteService::Instance();

    HttpResult result;
    auto reqJson = req->getJsonObject();
    if (!reqJson) {
        result.setResult(ApiError_InvalidJsonFormat);
        co_return makeJsonResp(result);
    }

    SqliteRpcRequest rpc;
    if (!buildRpcRequest(*reqJson, rpc, result)) {
        co_return makeJsonResp(result);
    }

    result = co_await _sqliteService->handleExecuteRpc(rpc);
    co_return makeJsonResp(result);
}

Task<HttpResponsePtr> SqliteController::BeginTransaction(HttpRequestPtr req) {
    auto _sqliteService = SQLiteService::Instance();

    HttpResult result;
    auto reqJson = req->getJsonObject();
    if (!reqJson) {
        result.setResult(ApiError_InvalidJsonFormat);
        co_return makeJsonResp(result);
    }

    std::string logicalName = readStringField(*reqJson, "logicalName");
    if (logicalName.empty() && (*reqJson).isMember("db") && (*reqJson)["db"].isObject()) {
        logicalName = readStringField((*reqJson)["db"], "logicalName");
    }

    result = co_await _sqliteService->beginTransaction(logicalName);
    co_return makeJsonResp(result);
}

Task<HttpResponsePtr> SqliteController::CommitTransaction(HttpRequestPtr req) {
    auto _sqliteService = SQLiteService::Instance();

    HttpResult result;
    auto reqJson = req->getJsonObject();
    if (!reqJson) {
        result.setResult(ApiError_InvalidJsonFormat);
        co_return makeJsonResp(result);
    }

    std::string txId = readStringField(*reqJson, "txId");
    result = co_await _sqliteService->commitTransaction(txId);
    co_return makeJsonResp(result);
}

Task<HttpResponsePtr> SqliteController::RollbackTransaction(HttpRequestPtr req) {
    auto _sqliteService = SQLiteService::Instance();

    HttpResult result;
    auto reqJson = req->getJsonObject();
    if (!reqJson) {
        result.setResult(ApiError_InvalidJsonFormat);
        co_return makeJsonResp(result);
    }

    std::string txId = readStringField(*reqJson, "txId");
    result = co_await _sqliteService->rollbackTransaction(txId);
    co_return makeJsonResp(result);
}

Task<HttpResponsePtr> SqliteController::ResolveLogicalName(HttpRequestPtr req) {
    auto _sqliteService = SQLiteService::Instance();

    HttpResult result;
    auto reqJson = req->getJsonObject();
    if (!reqJson) {
        result.setResult(ApiError_InvalidJsonFormat);
        co_return makeJsonResp(result);
    }

    std::string scope = readStringField(*reqJson, "scope");
    std::string projectCode = readStringField(*reqJson, "projectCode");
    std::string templateKind = readStringField(*reqJson, "templateKind");
    std::string fileName = readStringField(*reqJson, "fileName");

    // 兼容 dbNode.fileName / dbNode.projid
    if (fileName.empty() && (*reqJson).isMember("dbNode") && (*reqJson)["dbNode"].isObject()) {
        fileName = readStringField((*reqJson)["dbNode"], "fileName");
    }
    if (projectCode.empty() && (*reqJson).isMember("dbNode") && (*reqJson)["dbNode"].isObject()) {
        projectCode = readStringField((*reqJson)["dbNode"], "projid");
    }

    result = co_await _sqliteService->resolveLogicalName(scope, projectCode, templateKind, fileName);
    co_return makeJsonResp(result);
}

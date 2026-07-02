#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

/**
 * @brief SqliteController 提供本地 SQLite 文件的 SQL RPC 接口.
 *
 * 契约:
 *  - 所有接口都需要 JWT 通过 AuthFilter;
 *  - 请求体统一 application/json;
 *  - 服务端仅接收 SQLite 兼容 SQL, Access 方言由客户端在发送前转换.
 */
class SqliteController : public drogon::HttpController<SqliteController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(SqliteController::Query,             "/api/sqlite/query",       Post, "AuthFilter");
    ADD_METHOD_TO(SqliteController::Execute,           "/api/sqlite/exec",        Post, "AuthFilter");
    ADD_METHOD_TO(SqliteController::BeginTransaction,  "/api/sqlite/tx/begin",    Post, "AuthFilter");
    ADD_METHOD_TO(SqliteController::CommitTransaction, "/api/sqlite/tx/commit",   Post, "AuthFilter");
    ADD_METHOD_TO(SqliteController::RollbackTransaction, "/api/sqlite/tx/rollback", Post, "AuthFilter");
    ADD_METHOD_TO(SqliteController::ResolveLogicalName, "/api/sqlite/resolve",    Post, "AuthFilter");
    METHOD_LIST_END

    // POST /api/sqlite/query
    Task<HttpResponsePtr> Query(HttpRequestPtr req);

    // POST /api/sqlite/exec
    Task<HttpResponsePtr> Execute(HttpRequestPtr req);

    // POST /api/sqlite/tx/begin
    Task<HttpResponsePtr> BeginTransaction(HttpRequestPtr req);

    // POST /api/sqlite/tx/commit
    Task<HttpResponsePtr> CommitTransaction(HttpRequestPtr req);

    // POST /api/sqlite/tx/rollback
    Task<HttpResponsePtr> RollbackTransaction(HttpRequestPtr req);

    // POST /api/sqlite/resolve
    Task<HttpResponsePtr> ResolveLogicalName(HttpRequestPtr req);
};

# API Reference Documentation

## 1. 概览

本文档描述当前服务端仍注册的 HTTP 接口。

本服务端已不再承担用户管理职责，原有登录、注册、验证码、ActionToken、第三方登录、用户资料维护以及 GitLab 用户管理相关 HTTP 接口均已取消注册。客户端不应继续调用这些接口。

## 2. 认证方式

需要鉴权的接口通过 JWT 验证。

- Header: `Authorization: Bearer <jwt>`
- 服务端通过 `AuthFilter` 校验 JWT。
- JWT 校验失败时返回 401 状态码，响应体仍采用统一 JSON 错误结构。

## 3. 当前已注册接口

### 3.1 GitLab 项目邀请

- URL: `/api/gitlab/project/invite`
- Method: `POST`
- Auth: 需要 `Authorization: Bearer <jwt>`
- Content-Type: `application/json`

Body:

```json
{
  "userId": 123,
  "projectId": 456,
  "accessLevel": 30
}
```

字段说明:

- `userId`: 用户 ID。
- `projectId`: GitLab 项目 ID。
- `accessLevel`: GitLab 访问级别。

访问级别:

- `0`: NoAccess
- `5`: MinimalAccess
- `10`: Guest
- `20`: Reporter
- `30`: Developer
- `40`: Maintainer

成功响应:

```json
{
  "success": true
}
```

失败响应示例:

```json
{
  "success": false,
  "message": "用户GitLab信息不存在"
}
```

### 3.2 系统 Ping

- URL: `/system/ping`
- Method: `GET`
- Auth: 不需要

响应:

```json
{
  "code": 0,
  "msg": "pong",
  "data": {
    "serverTime": 1714348800
  }
}
```

### 3.3 SQLite 健康检查

- URL: `/system/sqlite/ping`
- Method: `GET`
- Auth: 不需要
- Description: 在默认 SQLite 库上执行 `SELECT 1`，确认 SQLiteService 已成功初始化并能正常读写。

响应示例:

```json
{
  "code": 0,
  "msg": "sqlite ok",
  "data": {
    "rowCount": 1,
    "rows": [
      { "ok": 1 }
    ]
  }
}
```

### 3.4 测试接口

- URL: `/test`
- Method: `GET`
- Auth: 不需要

该接口仅用于开发测试。

### 3.5 SQLite SQL RPC

所有 `/api/sqlite/*` 接口都需要 JWT，`Content-Type: application/json`。

**服务端仅接收 SQLite 兼容 SQL**。Access / Jet 方言的转换必须由客户端在发送前完成（例如通过桌面端的 `UEAccessSqliteDialect`）。请求体中不再区分 `dialect` 字段。

#### 3.5.1 参数化查询请求体

所有查询/执行接口共享同一份请求体结构：

```json
{
  "logicalName": "project.SAM.desi.0001_0001",
  "sql": "SELECT id, name FROM \"users\" WHERE name = ? LIMIT ?",
  "params": [
    { "type": "text", "value": "alice" },
    { "type": "int",  "value": 50 }
  ],
  "txId": null,
  "requestId": "req-2026-06-30-0001",
  "limit": 100,
  "offset": 0,
  "timeoutMs": 5000,
  "readOnly": true,
  "wantColumns": true
}
```

字段说明：

- `logicalName`（必填）：逻辑数据库名，服务端据此路由物理 `.sqlite`。也支持 `db.logicalName` 嵌套形式。
- `sql`（必填）：SQLite 兼容 SQL，使用 `?` 占位符。
- `params`（可选）：与 `?` 一一对应的参数数组。每项为 `{ "type": "int|real|text|blob|null", "value": ... }`。
- `txId`（可选）：事务 token，由 `/api/sqlite/tx/begin` 返回。
- `requestId`（可选）：幂等键，服务端在成功响应里回显。
- `limit`/`offset`（仅 query）：服务端在 SQL 未包含 `LIMIT` 时追加。
- `timeoutMs`（可选）：请求期望的最长执行时间（当前仅作为提示，未真正生效）。
- `readOnly`（可选）：仅在 query 上有意义；exec 上出现 `readOnly=true` 将被拒绝。
- `wantColumns`（可选）：是否返回列元信息（默认 true）。

#### 3.5.2 查询

- URL: `/api/sqlite/query`
- Method: `POST`

成功响应：

```json
{
  "code": 0,
  "msg": "success",
  "data": {
    "rows": [
      { "id": 1, "name": "alice" }
    ],
    "rowCount": 1,
    "requestId": "req-2026-06-30-0001"
  }
}
```

#### 3.5.3 执行（写）

- URL: `/api/sqlite/exec`
- Method: `POST`

`sql` 通常是 `INSERT / UPDATE / DELETE`。成功响应：

```json
{
  "code": 0,
  "msg": "success",
  "data": {
    "affectedRows": 1,
    "ok": true,
    "requestId": "req-2026-06-30-0001"
  }
}
```

#### 3.5.4 事务

同一逻辑库同一时刻只允许存在一个事务，重复 `begin` 会返回 `-708` 并附带现有 `txId`。

- 开启事务：
  - URL: `/api/sqlite/tx/begin`
  - Method: `POST`
  - Body: `{ "logicalName": "..." }`
  - 成功响应: `data.txId` 与 `data.expiresInSec`
- 提交事务：
  - URL: `/api/sqlite/tx/commit`
  - Method: `POST`
  - Body: `{ "txId": "..." }`
- 回滚事务：
  - URL: `/api/sqlite/tx/rollback`
  - Method: `POST`
  - Body: `{ "txId": "..." }`

在 `query` / `exec` 请求体里携带 `txId`，即表示该 SQL 在指定事务上下文中执行。若 `txId` 与 `logicalName` 不匹配，服务端返回 `-707`。

#### 3.5.5 逻辑名解析

- URL: `/api/sqlite/resolve`
- Method: `POST`
- Body：
  ```json
  {
    "scope": "project",
    "projectCode": "SAM",
    "templateKind": "desi",
    "fileName": "0001_0001",
    "dbNode": {
      "projid": "SAM",
      "fileName": "0001_0001"
    }
  }
  ```
- 成功响应：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "logicalName": "project.SAM.desi.0001_0001"
    }
  }
  ```

`scope` 取 `project` 或 `global`。当前逻辑名按 `<scope>.<projectCode>.<templateKind>[.<fileName>]` 拼装，后续可以接入 `core.project_databases` 元数据表做真正的映射。

## 4. 已取消注册的接口范围

以下接口已不再注册:

- `/user/login/*`
- `/user/token/verify/*`
- `/user/self`
- `/user/logout`
- `/user/create*`
- `/user/check_exist`
- `/user/mfa*`
- `/user/update`
- `/user/delete`
- `/user/action_token*`
- `/api/third/*`
- `/api/gitlab/user/*`

如需用户身份信息，应由上游认证系统签发 JWT，本服务端只负责校验 JWT 并保护业务接口。

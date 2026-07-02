# API Error Codes

当前服务端已不再注册用户管理相关 HTTP 接口，原登录、注册、验证码、ActionToken、第三方登录、用户资料维护以及 GitLab 用户管理接口的错误码说明已移除。

## 1. JWT 鉴权

使用 `AuthFilter` 的接口通过 `Authorization: Bearer <jwt>` 进行鉴权。

| 场景 | HTTP 状态 | 错误码 (Code) | 枚举名称 (Enum) | 说明/默认信息 |
| :--- | :--- | :--- | :--- | :--- |
| 缺少 Authorization | 200 | -203 | `ApiError_TokenMissing` | Authorization in header is missing or empty |
| JWT 无效或过期 | 401 | -201 | `ApiError_TokenInvalidOrExpired` | token已失效 |
| JWT 类型不符合接口要求 | 401 | -209 | `ApiError_TokenTypeUnexpected` | flashToken 不能直接用于认证 |
| JWT 类型未知 | 401 | -210 | `ApiError_TokenTypeInvalid` | 未知token类型 |

## 2. GitLab 项目接口

GitLab 项目接口当前仍使用 `{ "success": true/false, "message": "..." }` 响应格式。

| 接口路径 (Method) | 状态 | 错误信息 (Message) |
| :--- | :--- | :--- |
| **/api/gitlab/project/invite** (POST) | `success: false` | 请求体必须是JSON格式 |
| | `success: false` | 缺少必填项: ... |
| | `success: false` | 用户GitLab信息不存在 |
| | `success: false` | 无效的访问级别 |
| | `success: false` | 邀请用户加入GitLab项目失败 |

## 3. 系统模块

| 接口路径 (Method) | 错误码 (Code) | 枚举名称 (Enum) | 说明/默认信息 |
| :--- | :--- | :--- | :--- |
| **/system/ping** (GET) | 0 | `ApiError_Success` | Pong |
| | -104 | `ApiError_DatabaseError` | 数据库错误 |
| | -105 | `ApiError_InvalidOperation` | 无效操作 |
| | -106 | `ApiError_TooManyRequests` | 访问过于频繁，请稍后再试 |
| **/system/sqlite/ping** (GET) | 0 | `ApiError_Success` | SQLite 健康检查成功 |
| | -103 | `ApiError_InternalError` | SQLiteService 未就绪 |
| | -104 | `ApiError_DatabaseError` | SQLite 查询失败 |

## 4. SQLite RPC 模块

所有 `/api/sqlite/*` 接口共享下表错误码：

| 接口路径 (Method) | 错误码 (Code) | 枚举名称 (Enum) | 说明/默认信息 |
| :--- | :--- | :--- | :--- |
| **通用** | 0 | `ApiError_Success` | 成功 |
| | -101 | `ApiError_InvalidJsonFormat` | 请求体必须是 JSON 格式 |
| | -102 | `ApiError_MissingRequiredArgs` | 缺少必要参数 (resolve 时 scope/templateKind) |
| | -104 | `ApiError_DatabaseError` | SQLite 底层错误 |
| | -105 | `ApiError_InvalidOperation` | 不合法操作 (如 exec 请求带 readOnly=true) |
| | -201 / -203 / -209 / -210 | Token 类 | JWT 未通过 AuthFilter |
| **/api/sqlite/query** (POST) | -701 | `ApiError_SqliteRpc_MissingSql` | 缺少 sql 字段 |
| | -702 | `ApiError_SqliteRpc_ParamMismatch` | params 与 ? 数量不匹配 |
| | -703 | `ApiError_SqliteRpc_InvalidParamType` | params 元素格式非法 |
| | -704 | `ApiError_SqliteRpc_LogicalNameMissing` | 缺少 logicalName |
| | -705 | `ApiError_SqliteRpc_LogicalNameUnknown` | 未知逻辑数据库 |
| | -707 | `ApiError_SqliteRpc_TxIdInvalid` | 事务 token 与逻辑库不匹配 |
| | -709 | `ApiError_SqliteRpc_ExecuteFailed` | SQL 执行失败 |
| **/api/sqlite/exec** (POST) | (同上) | (同上) | (同上) |
| **/api/sqlite/tx/begin** (POST) | -704 | `ApiError_SqliteRpc_LogicalNameMissing` | 缺少 logicalName |
| | -705 | `ApiError_SqliteRpc_LogicalNameUnknown` | 未知逻辑数据库 |
| | -708 | `ApiError_SqliteRpc_TxAlreadyExists` | 该逻辑库已存在活动事务 |
| | -104 | `ApiError_DatabaseError` | BEGIN 失败 |
| **/api/sqlite/tx/commit** (POST) | -706 | `ApiError_SqliteRpc_TxIdMissing` | 缺少 txId |
| | -707 | `ApiError_SqliteRpc_TxIdInvalid` | 事务 token 无效或已过期 |
| | -104 | `ApiError_DatabaseError` | COMMIT 失败 |
| **/api/sqlite/tx/rollback** (POST) | -706 | `ApiError_SqliteRpc_TxIdMissing` | 缺少 txId |
| | -707 | `ApiError_SqliteRpc_TxIdInvalid` | 事务 token 无效或已过期 |
| | -104 | `ApiError_DatabaseError` | ROLLBACK 失败 |
| **/api/sqlite/resolve** (POST) | -102 | `ApiError_MissingRequiredArgs` | 缺少 scope 或 templateKind, scope=project 时缺少 projectCode |
| | -105 | `ApiError_InvalidOperation` | scope 非 project/global |

## 5. 已取消注册的接口范围

以下接口已取消注册，不再对外提供错误码契约:

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

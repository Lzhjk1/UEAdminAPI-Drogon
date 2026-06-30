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

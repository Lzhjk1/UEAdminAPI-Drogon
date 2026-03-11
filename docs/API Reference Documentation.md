# API Reference Documentation

## 1. 概览 (Overview)

本文档详细描述了 UEAdminAPI 的接口定义、参数说明及返回格式。

### 1.1 基础信息
- **协议**: HTTP/HTTPS
- **数据格式**: JSON
- **字符编码**: UTF-8

### 1.2 认证方式 (Authentication)
大多数接口需要鉴权，鉴权方式为在 HTTP Header 中添加 `Authorization` 字段。
- **Header**: `Authorization: Bearer <token>`
- **FlashToken**: 部分接口（如 `LoginByFlashToken`）使用 `AuthorizationFlashToken` 头。

### 1.3 响应格式 (Response Format)

#### 标准响应 (Standard Response)
绝大多数接口使用统一的响应格式：
```json
{
  "code": 0,          // 状态码，0 表示成功，非 0 表示错误
  "msg": "success",   // 提示信息
  "data": { ... }     // 业务数据
}
```
错误码定义请参考 [API_Error_Codes.md](API_Error_Codes.md)。

#### GitLab 模块响应 (GitLab Response)
GitLab 相关接口目前使用独立的响应格式：
```json
{
  "success": true,    // true/false
  "message": "...",   // 错误信息
  "data_field": ...   // 业务字段，如 gitlabId
}
```

---

## 2. 认证模块 (Authentication)

### 2.1 登录 (Login)

#### 2.1.1 账号密码登录
- **URL**: `/user/login/pwd`
- **Method**: `GET`
- **Params**:
  - `userName` (string, required): 用户名
  - `passWord` (string, required): 密码

#### 2.1.2 邮箱验证码登录
- **URL**: `/user/login/email`
- **Method**: `GET`
- **Params**:
  - `email` (string, required): 邮箱地址
  - `code` (string, required): 验证码

#### 2.1.3 手机验证码登录
- **URL**: `/user/login/phone`
- **Method**: `GET`
- **Params**:
  - `phone` (string, required): 手机号
  - `code` (string, required): 验证码

#### 2.1.4 FlashToken 登录
- **URL**: `/user/login/flashtoken`
- **Method**: `GET`
- **Headers**:
  - `AuthorizationFlashToken`: FlashToken 字符串

#### 2.1.5 验证 Token
- **URL**: `/user/token/verify`
- **Method**: `GET`
- **Headers**:
  - `Authorization`: Token
- **Description**: 验证 Token 是否有效，并返回 Token 相关信息。

#### 2.1.6 获取个人信息
- **URL**: `/user/self`
- **Method**: `GET`
- **Headers**:
  - `Authorization`: Token

### 2.2 注册 (Register)

#### 2.2.1 账号注册 (通用)
- **URL**: `/user/create`
- **Method**: `POST`
- **Content-Type**: `application/json`
- **Body**:
  ```json
  {
    "username": "user1",       // (Required) 用户名
    "user_password": "pwd",    // (Required) 密码
    "verifyCode": "123456",    // (Required) 验证码
    "email": "test@ex.com",    // (Required if phone missing) 邮箱
    "tel": "13800000000",      // (Required if email missing) 手机号
    "nickname": "Nick",        // (Optional) 昵称
    "privilege": "User",       // (Optional) 权限, 默认 User
    "isMale": "true",          // (Optional) 性别, 默认 true
    "third_platform_name": "", // (Optional) 第三方平台名称
    "third_code": "",          // (Required if platform set) 第三方 Code
    "third_verifyCode": ""     // (Required if platform set) 第三方 VerifyCode
  }
  ```

#### 2.2.2 手机快速注册
- **URL**: `/user/create/phone?phone={phone}&verifyCode={code}`
- **Method**: `POST`
- **Params**:
  - `phone` (string, required): 手机号
  - `verifyCode` (string, required): 验证码
- **Response**:
  ```json
  {
    "code": 0,
    "msg": "注册成功...",
    "data": {
      "username": "...",
      "password": "..."
    }
  }
  ```

#### 2.2.3 检查用户是否存在
- **URL**: `/user/check_exist?target={target}`
- **Method**: `GET`
- **Headers**:
  - `Authorization`: Token
- **Params**:
  - `target` (string, required): 邮箱或手机号
- **Response**:
  ```json
  {
    "code": 0,
    "msg": "用户已存在" / "用户不存在",
    "data": {
      "exist": true / false
    }
  }
  ```

### 2.3 验证码 (Verification Code)

#### 2.3.1 发送验证码
- **URL**: `/user/mfa`
- **Method**: `GET`
- **Params**:
  - `target` (string, required): 邮箱或手机号
  - `type` (string, required): 类型 (通常由后端自动识别，但参数名为 type)

---

## 3. 用户管理 (User Management)

### 3.1 更新用户信息
- **URL**: `/user/update`
- **Method**: `POST`
- **Headers**: `Authorization`
- **Body**:
  ```json
  {
    "verifyCode": "...",             // (Required) 当前绑定方式的验证码
    "target": "...",                 // (Required) 验证码发送的目标(邮箱/手机)
    "username": "new_name",          // (Optional)
    "nickname": "new_nick",          // (Optional)
    "email": "new@ex.com",           // (Optional)
    "tel": "139...",                 // (Optional)
    "newEmailOrPhoneVerifyCode": "", // (Required if email/tel changed) 新邮箱/手机的验证码
    "isMale": "false",               // (Optional)
    "user_password": "..."           // (Optional)
  }
  ```

### 3.2 删除用户
- **URL**: `/user/delete`
- **Method**: `POST`
- **Headers**: `Authorization`
- **Params (Query)**:
  - `verifyCode` (string, required): 验证码
  - `target` (string, required): 验证码目标

---

## 4. 第三方登录 (Third-Party Login)

### 4.1 获取授权 URL
- **URL**: `/api/third/authorization_url`
- **Method**: `GET`
- **Params**:
  - `platform` (string, required): 平台名称 (如 `github`, `gitlab` 等)

### 4.2 登录回调 (Callback)
- **URL**: `/api/third/{platform}`
- **Method**: `GET`
- **Params**:
  - `code`: 授权码
  - `state`: 状态码
- **Description**: 第三方平台重定向回来的地址。

### 4.3 验证登录
- **URL**: `/api/third/login/check`
- **Method**: `GET`
- **Params**:
  - `platform`, `code`, `verifyCode`, `onlyCheck`

### 4.4 直接登录
- **URL**: `/api/third/login`
- **Method**: `GET`
- **Params**:
  - `platform`, `code`, `verifyCode`

### 4.5 绑定账号
- **URL**: `/api/third/bind`
- **Method**: `POST`
- **Headers**: `Authorization`
- **Params (Query)**:
  - `platform`, `code`, `verifyCode`

### 4.6 第三方注册
- **URL**: `/api/third/register`
- **Method**: `POST`
- **Params (Query)**:
  - `platform`, `code`, `verifyCode`

### 4.7 解绑账号
- **URL**: `/api/third/unbind`
- **Method**: `POST`
- **Headers**: `Authorization`
- **Params (Query)**:
  - `platform`, `mfaTarget`, `verifyCode`

---

## 5. GitLab 集成 (GitLab Integration)

> 注意：本模块返回格式为 `{ "success": true/false, "message": "..." }`

### 5.1 创建 GitLab 用户
- **URL**: `/api/gitlab/user/create`
- **Method**: `POST`
- **Body**:
  ```json
  {
    "userId": 123,       // (Required) 本地用户ID
    "password": "...",   // (Required) GitLab 密码
    "email": "..."       // (Optional)
  }
  ```

### 5.2 删除 GitLab 用户
- **URL**: `/api/gitlab/user/delete`
- **Method**: `POST`
- **Body**:
  ```json
  { "userId": 123 }
  ```

### 5.3 修改密码
- **URL**: `/api/gitlab/user/password`
- **Method**: `POST`
- **Body**:
  ```json
  { "userId": 123, "password": "new_password" }
  ```

### 5.4 创建 Impersonation Token
- **URL**: `/api/gitlab/user/token/create`
- **Method**: `POST`
- **Body**:
  ```json
  { "userId": 123 }
  ```
- **Response Success**: `{ "success": true, "token": "..." }`

### 5.5 删除 Impersonation Token
- **URL**: `/api/gitlab/user/token/delete`
- **Method**: `POST`
- **Body**:
  ```json
  { "userId": 123 }
  ```

### 5.6 邀请加入项目
- **URL**: `/api/gitlab/project/invite`
- **Method**: `POST`
- **Body**:
  ```json
  {
    "userId": 123,       // (Required)
    "projectId": 456,    // (Required)
    "accessLevel": 30    // (Required) 访问级别
  }
  ```
- **Access Levels**:
  - `0`: NoAccess
  - `5`: MinimalAccess
  - `10`: Guest
  - `20`: Reporter
  - `30`: Developer
  - `40`: Maintainer

---

## 6. 系统接口 (System)

### 6.1 Ping
- **URL**: `/system/ping`
- **Method**: `GET`
- **Response**:
  ```json
  { "code": 0, "msg": "success", "data": "Pong" }
  ```
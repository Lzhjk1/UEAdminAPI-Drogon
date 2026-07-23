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
- **ActionToken**: 部分高危操作（如修改敏感信息、注销、绑定解绑第三方、验证码登录等）还需要在 HTTP Header 中添加 `X-Action-Token` 字段。

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
- **URL**: `/api/user/login/pwd`
- **Method**: `GET`
- **Params**:
  - `userName` (string, required): 用户名
  - `passWord` (string, required): 密码
- **Response**:
  ```json
  {
    "code": 0,
    "msg": "登录成功",
    "data": {
      "id": 1,
      "token": "eyJhbGci...",
      "flashToken": "eyJhbGci..."
    }
  }
  ```

#### 2.1.2 邮箱验证码登录
- **URL**: `/api/user/login/email`
- **Method**: `GET`
- **Headers**:
  - `X-Action-Token`: (Required) 操作授权令牌，通过 `/api/user/action_token/anonymous` (mfaType: "Login") 获取
- **Description**: `X-Action-Token` 绑定了申请时的 `target`，此处 `email` 必须与申请匿名 ActionToken 时使用的 `target` 一致（邮箱大小写不敏感）。
- **Params**:
  - `email` (string, required): 邮箱地址
- **Response**:
  ```json
  {
    "code": 0,
    "msg": "登录成功",
    "data": {
      "id": 1,
      "token": "eyJhbGci...",
      "flashToken": "eyJhbGci..."
    }
  }
  ```

#### 2.1.3 手机验证码登录
- **URL**: `/api/user/login/phone`
- **Method**: `GET`
- **Headers**:
  - `X-Action-Token`: (Required) 操作授权令牌，通过 `/api/user/action_token/anonymous` (mfaType: "Login") 获取
- **Description**: `X-Action-Token` 绑定了申请时的 `target`，此处 `phone` 必须与申请匿名 ActionToken 时使用的 `target` 一致。
- **Params**:
  - `phone` (string, required): 手机号
- **Response**:
  ```json
  {
    "code": 0,
    "msg": "登录成功",
    "data": {
      "id": 1,
      "token": "eyJhbGci...",
      "flashToken": "eyJhbGci..."
    }
  }
  ```

#### 2.1.4 FlashToken 登录
- **URL**: `/api/user/login/flashtoken`
- **Method**: `GET`
- **Headers**:
  - `AuthorizationFlashToken`: FlashToken 字符串
- **Response**:
  ```json
  {
    "code": 0,
    "msg": "登录成功",
    "data": {
      "id": 1,
      "token": "eyJhbGci...",
      "flashToken": "eyJhbGci..."
    }
  }
  ```

#### 2.1.5 验证 FlashToken
- **URL**: `/api/user/token/verify/flash`
- **Method**: `GET`
- **Params**:
  - `token` (string, required): FlashToken 字符串
- **Description**: 验证 FlashToken 是否有效。
- **Response**:
  ```json
  {
    "code": 0,
    "msg": "FlashToken验证成功",
    "data": {
      "userId": 1,
      "tokenType": "flashToken",
      "status": 123
    }
  }
  ```

#### 2.1.6 验证 AuthToken
- **URL**: `/api/user/token/verify/auth`
- **Method**: `GET`
- **Params**:
  - `token` (string, required): AuthToken 字符串
- **Description**: 验证 AuthToken 是否有效。
- **Response**:
  ```json
  {
    "code": 0,
    "msg": "Token验证成功",
    "data": {
      "userId": 1,
      "tokenType": "token",
      "status": 123
    }
  }
  ```

#### 2.1.7 获取个人信息
- **URL**: `/api/user/self`
- **Method**: `GET`
- **Headers**:
  - `Authorization`: Token
- **Response**:
  ```json
  {
    "code": 0,
    "data": {
        "configured_third_platform_name": [],
        "created_at": "20260331 10:01:45",
        "email": "everspace@qq.com",
        "gitlab_token": "glpat-xxxxxxxxxxxxxxxxx",
        "id": 33,
        "sex": "man",
        "tel": "",
        "userName": "TestAccount",
        "userNick": "TestAccount"
    },
    "msg": "success"
  }
  ```

#### 2.1.8 注销登录
- **URL**: `/api/user/logout`
- **Method**: `POST`
- **Headers**:
  - `Authorization`: Token (需要注销的 Token)
- **Response**:
  ```json
  {
    "code": 0,
    "msg": "注销成功",
    "data": null
  }
  ```

### 2.2 注册 (Register)

#### 2.2.1 账号注册 (通用)
- **URL**: `/api/user/create`
- **Method**: `POST`
- **Headers**:
  - `X-Action-Token`: (Required) 操作授权令牌，通过 `/api/user/action_token/anonymous` (mfaType: "Register") 获取
- **Description**: `X-Action-Token` 绑定了申请时的 `target`，请求体中的 `email` 或 `phone/tel` 需与该 `target` 一致。
- **Content-Type**: `application/json`
- **Body**:
  ```json
  {
    "username": "user1",       // (Required) 用户名
    "user_password": "pwd",    // (Required) 密码
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
- **Response**:
  ```json
  {
    "code": 0,
    "msg": "创建用户成功",
    "data": {
      "userId": 2
    }
  }
  ```

#### 2.2.2 手机快速注册
- **URL**: `/api/user/create/phone?phone={phone}`
- **Method**: `POST`
- **Headers**:
  - `X-Action-Token`: (Required) 操作授权令牌，通过 `/api/user/action_token/anonymous` (mfaType: "Register") 获取
- **Description**: `X-Action-Token` 绑定了申请时的 `target`，此处 `phone` 必须与申请匿名 ActionToken 时使用的 `target` 一致。
- **Params**:
  - `phone` (string, required): 手机号
- **Response**:
  ```json
  {
    "code": 0,
    "msg": "注册成功",
    "data": {
      "username": "...",
      "password": "..."
    }
  }
  ```

#### 2.2.3 邮箱快速注册
- **URL**: `/api/user/create/email?email={email}`
- **Method**: `POST`
- **Headers**:
  - `X-Action-Token`: (Required) 操作授权令牌，通过 `/api/user/action_token/anonymous` (mfaType: "Register") 获取
- **Description**: `X-Action-Token` 绑定了申请时的 `target`，此处 `email` 必须与申请匿名 ActionToken 时使用的 `target` 一致。
- **Params**:
  - `email` (string, required): 邮箱
- **Response**:
  ```json
  {
    "code": 0,
    "msg": "注册成功",
    "data": {
      "username": "...",
      "password": "..."
    }
  }
  ```

#### 2.2.4 检查用户是否存在
- **URL**: `/api/user/check_exist?target={target}`
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
- **URL**: `/api/user/mfa`
- **Method**: `GET`
- **Params**:
  - `target` (string, required): 邮箱或手机号
  - `type` (string, required): 类型 (通常由后端自动识别，但参数名为 type)
- **Response**:
  ```json
  {
    "code": 0,
    "msg": "验证码发送成功.",
    "data": null
  }
  ```

#### 2.3.2 检查验证码
- **URL**: `/api/user/mfa/check`
- **Method**: `GET`
- **Params**:
  - `target` (string, required): 邮箱或手机号
  - `mfaCode` (string, required): 验证码
  - `type` (string, required): 类型，需与发送验证码时的类型一致
- **Description**: 仅检查验证码是否正确，但不消耗该验证码，后续操作依然可以使用此验证码。
- **Response**:
  ```json
  {
    "code": 0,
    "msg": "验证码正确",
    "data": null
  }
  ```

---

## 3. 用户管理 (User Management)

### 3.1 更新用户信息
- **URL**: `/api/user/update`
- **Method**: `POST`
- **Headers**: 
  - `Authorization`: Bearer Token
  - `X-Action-Token`: (Required) 操作授权令牌，通过 `/api/user/action_token` (mfaType: "ModifyUser") 获取
- **Body**:
  ```json
  {
    "username": "new_name",          // (Optional)
    "nickname": "new_nick",          // (Optional)
    "email": "new@ex.com",           // (Optional)
    "tel": "139...",                 // (Optional)
    "newMfaCode": "...",             // (Required if email/tel changed or unbind) 新邮箱/手机的验证码或解绑操作对应的验证码
    "unbindEmail": "false",          // (Optional) 是否解绑邮箱
    "unbindPhone": "false",          // (Optional) 是否解绑手机
    "isMale": "false",               // (Optional)
    "user_password": "..."           // (Optional)
  }
  ```
- **Response**:
  ```json
  {
    "code": 0,
    "msg": "更新成功",
    "data": null
  }
  ```

### 3.2 删除用户
- **URL**: `/api/user/delete`
- **Method**: `DELETE`
- **Headers**: 
  - `Authorization`: Bearer Token
  - `X-Action-Token`: ActionToken (需先通过 `/api/user/action_token` 获取)
- **Description**: 删除当前登录用户。需要先获取 `Action_DeleteUser` 类型的 ActionToken。用户的基本信息将被移动到 `user_deleted` 表中进行软删除，而第三方、GitLab 等关联信息将被彻底删除。
- **Response**:
  ```json
  {
    "code": 0,
    "msg": "删除成功",
    "data": null
  }
  ```

### 3.3 获取操作授权令牌 (ActionToken)
#### 3.3.1 获取操作授权令牌 - 登录后
- **URL**: `/api/user/action_token`
- **Method**: `GET`
- **Headers**: `Authorization`
- **Params (Query)**:
  - `mfaType` (string, required): 业务类别，如 "DeleteUser", "Unbind", "ModifyUser", "ThirdPartyBind"
  - `mfaCode` (string, optional): MFA 验证码 (如果用户绑定了邮箱或手机号则必填)
  - `target` (string, optional): 验证码发送的目标(邮箱/手机) (如果用户绑定了邮箱或手机号则必填)
- **Description**: 为高危操作（如删除用户、解绑第三方账号等）申请一个一次性的不透明随机 Token。如果用户未绑定邮箱或手机号，无需提供 `mfaCode` 和 `target`。该 Token 的有效期为 5 分钟。
- **Response**:
  ```json
  {
    "code": 0,
    "msg": "ActionToken 颁发成功",
    "data": {
      "actionToken": "...",
      "mfaType": "...",
      "expiresIn": 300
    }
  }
  ```

#### 3.3.2 获取操作授权令牌 - 登录前 (匿名)
- **URL**: `/api/user/action_token/anonymous`
- **Method**: `GET`
- **Params (Query)**:
  - `mfaType` (string, required): 业务类别，必须为 "Login", "Register", "LoginOrRegister", 或 "ResetPassword"
  - `mfaCode` (string, required): MFA 验证码
  - `target` (string, required): 验证码发送的目标(邮箱/手机)
- **Description**: 在未登录状态下（如登录、注册前）验证手机/邮箱验证码，并获取 ActionToken。获取的 Token 与 `userId = -1` 绑定，并绑定到本次传入的 `target`；后续受保护接口中提交的目标值必须与该 `target` 一致。
- **Response**: 同上。

---

## 4. 第三方登录 (Third-Party Login)

### 4.1 获取授权 URL
- **URL**: `/api/third/authorization_url`
- **Method**: `GET`
- **Params**:
  - `platform` (string, required): 平台名称 (目前有 `qq`, `wechat` 两个平台)
- **Response**:
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "authorizationUrl": "https://...", // 第三方授权页面地址
      "code": "...",                     // 临时会话 Code (用于后续接口)
      "verifyCode": "..."                // 临时会话 VerifyCode (用于后续接口)
    }
  }
  ```
- **Description**: 获取第三方登录的授权地址。返回的 `code` 和 `verifyCode` 必须保存，用于后续的 `login/check`, `bind`, `register` 等接口。客户端应引导用户在浏览器中打开 `authorizationUrl`。

### 4.2 登录回调 (Callback)
- **URL**: `/api/third/{platform}`
- **Method**: `GET`
- **Params**:
  - `code`: OAuth 授权码
  - `state`: OAuth 状态码 (包含服务器生成的 verify info)
- **Description**: 第三方平台授权完成后重定向回来的地址。通常由浏览器自动访问。服务器接收到此请求后，会将 OAuth 信息与 4.1 中生成的临时会话关联。

### 4.3 验证登录 (Check Login Status)
- **URL**: `/api/third/login/check`
- **Method**: `GET`
- **Params**:
  - `platform` (string, required): 平台名称
  - `code` (string, required): 4.1 接口返回的 `code`
  - `verifyCode` (string, required): 4.1 接口返回的 `verifyCode`
  - `onlyCheck` (boolean, optional): 默认为 `true`。
    - `true`: 仅检查是否绑定。
    - `false`: 如果已绑定，直接执行登录逻辑并返回 Token。
- **Response (onlyCheck=true)**:
  ```json
  {
    "code": 0,
    "msg": "验证成功",
    "data": {
      "allready_bind": true // true: 已绑定本地账号; false: 未绑定 (需调用 bind 或 register)
    }
  }
  ```
- **Response (onlyCheck=false, allready_bind=true)**:
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "token": "...",       // 登录 Token
      "user": { ... }       // 用户信息
    }
  }
  ```
- **Response (onlyCheck=false, allready_bind=false)**:
  ```json
  {
    "code": -503,
    "msg": "该平台未绑定过该账号",
    "data": {
      "allready_bind": false
    }
  }
  ```

### 4.4 直接登录 (Direct Login)
- **URL**: `/api/third/login`
- **Method**: `GET`
- **Params**:
  - `platform` (string, required): 平台名称
  - `code` (string, required): 4.1 接口返回的 `code`
  - `verifyCode` (string, required): 4.1 接口返回的 `verifyCode`
- **Description**: 尝试直接使用第三方账号登录。如果未绑定，将返回错误。
- **Response (Success)**:
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "token": "...",
      "user": { ... }
    }
  }
  ```
- **Response (Error)**:
  ```json
  {
    "code": -503,
    "msg": "该平台未绑定过该账号",
    "data": null
  }
  ```

### 4.5 绑定账号 (Bind Account)
- **URL**: `/api/third/bind`
- **Method**: `POST`
- **Headers**:
  - `Authorization`: Bearer Token (必须已登录)
  - `X-Action-Token`: (Required) 操作授权令牌，通过 `/api/user/action_token` (mfaType: "ThirdPartyBind") 获取
- **Params (Query)**:
  - `platform` (string, required): 平台名称
  - `code` (string, required): 4.1 接口返回的 `code`
  - `verifyCode` (string, required): 4.1 接口返回的 `verifyCode`
- **Description**: 将当前登录用户与第三方账号绑定。
- **Response**:
  ```json
  {
    "code": 0,
    "msg": "绑定成功",
    "data": null
  }
  ```
- **Error Codes**:
  - `-502`: 该平台已经绑定过账号

### 4.6 第三方注册 (Register via Third-Party)
- **URL**: `/api/third/register`
- **Method**: `POST`
- **Params (Query)**:
  - `platform` (string, required): 平台名称
  - `code` (string, required): 4.1 接口返回的 `code`
  - `verifyCode` (string, required): 4.1 接口返回的 `verifyCode`
- **Description**: 使用第三方账号信息创建一个新的本地用户，并自动登录。
- **Response**:
  ```json
  {
    "code": 0,
    "data": {
        "flashToken": "eyJhb...",
        "id": 37,
        "token": "eyJhb..."
    },
    "msg": "登录成功"
  }
  ```
- **Error Codes**:
  - `-502`: 该账号已经被绑定 (不能重复注册)

### 4.7 解绑账号 (Unbind Account)
- **URL**: `/api/third/unbind`
- **Method**: `POST`
- **Headers**:
  - `Authorization`: Bearer Token
  - `X-Action-Token`: (Required) 操作授权令牌，通过 `/api/user/action_token` (mfaType: "Unbind") 获取
- **Params (Query)**:
  - `platform` (string, required): 平台名称
- **Description**: 解除当前用户与指定第三方平台的绑定。
- **Response**:
  ```json
  {
    "code": 0,
    "msg": "解绑成功",
    "data": null
  }
  ```

---

## 5. GitLab 集成 (GitLab Integration) (未使用, 不测试)

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
- **URL**: `/api/system/ping`
- **Method**: `GET`
- **Response**:
  ```json
  {
    "code": 0,
    "msg": "pong",
    "data": {
      "serverTime": 1714348800
    }
  }
  ```

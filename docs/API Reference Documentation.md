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
    "msg": "刷新成功",
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
  - `flashToken` (string, required): FlashToken 字符串
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
    "password": "pwd",         // (Required) 密码
    "email": "test@ex.com",    // (Required if phone missing) 邮箱
    "phone": "13800000000",    // (Required if email missing) 手机号
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
    "msg": "注册成功, 请尽快修改您的密码",
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
    "msg": "注册成功, 请尽快修改您的密码",
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
  - `ueadmin_state` (string, optional): 设备登录会话 state。当从 `/login` 设备登录页发起第三方登录时传入；服务端会**把设备 state 记在该次第三方登录会话里**（`ThirdPartyLoginValue.deviceState`），第三方回调时按 `code` 反查，据此跳转回 loopback `redirect_uri`。不传则保持旧版 ueclient 兼容流程。
    > 注意：设备 state **不会**被追加到 `authorizationUrl` 上。第三方平台回调只带回 `code` 和 `state`（`redirect_uri` 也是固定值），任何附加的自定义 query 参数都会被平台丢弃，所以只能存在服务端。
- **Response**:
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "authorizationUrl": "https://graph.qq.com/oauth2.0/authorize?...&state=<code>",
      "code": "...",                     // 临时会话 Code (用于后续接口，同时作为 OAuth 的 state)
      "verifyCode": "...",               // 临时会话 VerifyCode (用于后续接口)
      "state": "...",                    // 设备登录 state（仅传入 ueadmin_state 时返回）
      "appId": "...",                    // 第三方平台 appid（公开值），供登录页在页内渲染二维码
      "redirectUri": "https://..."       // 第三方回调地址，wxLogin.js 需要它
    }
  }
  ```
- **Description**: 获取第三方登录的授权地址。返回的 `code` 和 `verifyCode` 必须保存，用于后续的 `login/check`, `bind`, `register` 等接口。客户端应引导用户在浏览器中打开 `authorizationUrl`。
  - 登录页在**页内**渲染二维码时用 `appId` + `redirectUri` + `code`（作为 `state`）实例化微信官方 `wxLogin.js`；`authorizationUrl` 作为降级跳转地址。
  - `clientSecret` 属机密，任何情况下都不会经接口返回。
- **错误码**:
  - `-501` 不支持的第三方平台
  - `-701` `ueadmin_state` 不存在、已过期或已消费（由 `DeviceLoginOptional` 过滤器返回）

### 4.2 登录回调 (Callback)
- **URL**: `/api/third/{platform}`
- **Method**: `GET`
- **Params**:
  - `code`: OAuth 授权码
  - `state`: OAuth 状态码（即 4.1 返回的临时会话 `code`；服务端据此反查该次登录会话，包括其中记录的设备登录 state）
- **Description**: 第三方平台授权完成后重定向回来的地址。通常由浏览器自动访问。服务器接收到此请求后，会将 OAuth 信息与 4.1 中生成的临时会话关联。
  - 该次登录由设备登录页发起（4.1 传了 `ueadmin_state`）且对应设备会话存在 loopback `redirect_uri` 时：
    - 该第三方账号**已绑定**本地用户 → 把 `userId` 写回设备会话，渲染 `login_redirect.csp` 跳转到该 loopback 地址（**仅通知，不传 token**），客户端随后用 `/api/oauth2/login/check` 取 token；
    - **未绑定**（或回调本身未完成/已过期）→ 渲染 `third_party_unbound.csp` 提示页并**不跳转**。跳了会让浏览器显示"验证成功"而客户端永远等不到 token。
  - 不带 `ueadmin_state` 时保留旧行为：跳转 `ueloginreturn://success?state=<第三方state>`（PDMS / 旧 ueclient 依赖此行为）。

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

---

## 7. OAuth2 端点 (OAuth2 Endpoints)

> 本节接口符合标准 OAuth2 规范，用于 Token 验证、吊销及公钥分发。

### 7.1 JWKS 公钥端点
- **URL**: `/.well-known/jwks.json`
- **Method**: `GET`
- **Description**: 返回 RSA 公钥的 JWKS (JSON Web Key Set) 格式，用于 OAuth2 客户端验证 Token 签名。建议客户端缓存此响应，缓存时长由 `Cache-Control` 头指定。
- **Response**:
  ```json
  {
    "keys": [
      {
        "kty": "RSA",
        "alg": "RS256",
        "use": "sig",
        "kid": "ueadmin-rsa-1",
        "n": "...",
        "e": "..."
      }
    ]
  }
  ```

### 7.2 Token 内省端点 (Introspection)
- **URL**: `/api/oauth2/introspect`
- **Method**: `POST`
- **Description**: 验证 Token 的有效性并返回详细信息。接受 `token` 参数（POST body 或 form 格式）。
- **Params**:
  - `token` (string, required): 要验证的 AuthToken 或 FlashToken
- **Response (active=true)**:
  ```json
  {
    "active": true,
    "sub": "33",
    "user_id": 33,
    "token_type": "Bearer",
    "username": "TestAccount",
    "nickname": "TestAccount",
    "email": "test@qq.com",
    "roles": ["user"],
    "exp": 1714348800
  }
  ```
- **Response (active=false)**:
  ```json
  {
    "active": false
  }
  ```

### 7.3 Token 吊销端点 (Revocation)
- **URL**: `/api/oauth2/revoke`
- **Method**: `POST`
- **Description**: 吊销指定的 Token，使其立即失效。接受 `token` 参数（POST body 或 form 格式）。
- **Params**:
  - `token` (string, required): 要吊销的 Token
- **Response**:
  ```json
  {
    "result": "ok"
  }
  ```

### 7.4 设备登录发起 (Device Login Start)
- **URL**: `/api/oauth2/login/start`
- **Method**: `POST`
- **Body**: `{ "redirect_uri": "http://127.0.0.1:45678/cb" }`
- **Description**: 发起 OAuth2 设备登录会话，用于 Pidgin 等客户端“浏览器登录 → 轮询取 token”。`redirect_uri` 仅允许 loopback 地址（host 严格为 `127.0.0.1` / `localhost` / `[::1]`，scheme 仅 `http`）；为空时兼容旧 `ueloginreturn://` 流程。
- **Response (Success)**:
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "state": "ab12cd34...",
      "login_url": "/login?state=ab12cd34...&redirect_uri=http%3A%2F%2F127.0.0.1%3A45678%2Fcb",
      "expires": 600
    }
  }
  ```
- **错误码**:
  - `-703` `redirect_uri` 非法（非严格 loopback）
  - `-103` 服务未初始化

### 7.5 设备登录状态轮询 (Device Login Check)
- **URL**: `/api/oauth2/login/check?state={1}`
- **Method**: `GET`
- **Params**:
  - `state` (string, required): `login/start` 返回的一次性 state
- **Description**: 轮询设备登录结果。未登录完成返回 `active:false` 且不消费 state（可继续轮询）；登录完成后返回 `active:true` 并携带 `token`、`flashToken`、`username`，**取后即废**（第二次调用返回 `-702`）。
- **Response (未完成)**:
  ```json
  { "code": 0, "msg": "success", "data": { "active": false } }
  ```
- **Response (已完成)**:
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "active": true,
      "token": "...",
      "flashToken": "...",
      "username": "roasal"
    }
  }
  ```
- **错误码**:
  - `-701` state 不存在、已过期或已消费
  - `-702` state 已被其他请求消费（含并发轮询抢占）
  - `-302` 用户不存在（用户已删除）
  - `-103` 查询用户失败
  - `-308` 更新失败

### 7.6 设备登录页 (Device Login Page)
- **URL**: `/login?state={1}&redirect_uri={2}`
- **Method**: `GET`
- **Description**: 渲染设备登录 HTML 页面。提供两个 tab：
  - **账号密码**：原生表单 POST 到 `/login`（同源，避开 fetch 的 CORS 预检）。
  - **扫码登录**：首次切到该 tab 时调 `GET /api/third/authorization_url?platform=<qq|wechat>&ueadmin_state=<state>` 建一条第三方登录会话；微信用官方 `wxLogin.js` 在页内渲染二维码（`self_redirect=false`，扫码确认后整页跳到 `/api/third/wechat`），QQ 因无官方页内二维码组件改为跳转 QQ 登录页。二维码 2 分钟有效，页面提供「刷新二维码」。
  - 两个平台各自持有一条第三方登录记录、都指向同一个设备会话，谁先扫码完成都能让该会话登录。
- **Params**:
  - `state` (string, required): `login/start` 返回的 state
  - `redirect_uri` (string, optional): 登录成功后的 loopback 通知地址
- **Response**: HTML 页面。state 无效/过期返回 400。

### 7.7 设备登录页密码登录 (Device Login By Password)
- **URL**: `/login`
- **Method**: `POST`
- **Body**: 支持两种提交格式
  - JSON: `{ "state": "...", "userName": "...", "passWord": "...", "redirect_uri": "..." }`
  - 原生 HTML 表单: `application/x-www-form-urlencoded`（登录页使用原生表单导航，避免跨域预检）；字段名与 JSON 相同
- **Description**: 设备登录页的账号密码登录。登录成功把 `userId` 写入设备会话，并 302 跳转到创建会话时保存的 `redirect_uri`（loopback，仅通知）；请求体里的 `redirect_uri` 必须与会话保存值一致，否则拒绝。未传 `redirect_uri` 时跳转 `ueloginreturn://success?state=<state>` 兼容旧客户端。
- **Response**:
  - 成功: 302 重定向到通知地址
  - 失败（JSON 请求）: JSON 错误
  - 失败（表单提交）: 302 回登录页 `/login?state=...&redirect_uri=...&error=<错误信息>`，由登录页展示错误（原生表单导航无法读取 JSON 响应体）
- **错误码**:
  - `-101` 请求体非 JSON
  - `-102` 缺少参数
  - `-701` state 不存在或已过期
  - `-702` 该登录会话已完成，请直接使用 login/check 获取 token
  - `-703` redirect_uri 与登录会话不一致
  - `-301` 用户名或密码错误

### 7.8 设备登录完成页 (Device Login Done)
- **URL**: `/login/done`
- **Method**: `GET`
- **Description**: 登录完成提示页（✔ 登录成功 / 可以关闭此浏览器窗口）。客户端 loopback 收到通知后不再返回纯文本，而是 302 跳到这里展示结果。
- **Params**: 无。
- **Response**: HTML 页面。**不做任何状态校验** —— 客户端收到 loopback 通知后会立刻轮询 `login/check`，此时 state 可能已被一次性消费，所以这一页必须与会话解耦。


## 8. 账号中心 / 团队 / 授权（阶段 1）

> 登录后客户端只调 `bootstrap` 一个接口即可拿到余额、团队、付费期、角色与云端功能授权；
> 团队管理由团队管理员在客户端内完成；付费期与功能授权由厂商超管在后台设置。
> 厂商超管的判据有两条（满足其一即可）：`privilege` 的 `Admin` 位，或系统角色 `super_admin`（见 8.7–8.11）。
>
> 说明：`POST /api/team/member` 目前只要求登录（`AuthFilter`），未强制 ActionToken —— 团队管理员未必绑定 MFA。
> 后续接入时在路由上补 `ActionTokenMiddleware` 并在 `services/ActionTokenService.cpp` 登记同样路由。

### 8.1 账号中心聚合 (Bootstrap)
- **URL**: `/api/me/bootstrap`
- **Method**: `GET`
- **Header**: `Authorization: Bearer {token}`
- **Response**:
```json
{
  "code": 0, "msg": "success",
  "data": {
    "userId": 12, "privilege": 3, "isSuperAdmin": false,
    "mustChangePwd": false, "balanceCents": 0,
    "team": { "id": 5, "name": "Team-5-acme", "status": 1, "roleCode": "team_admin" },
    "subscription": { "startDate": "2026-10-26", "endDate": "2027-10-26", "active": true },
    "featureAuth": { "cloud_project": true, "online_update": false },
    "latestVersion": ""
  }
}
```
- 无团队时 `team` 字段缺失；无付费记录时 `startDate`/`endDate` 为空串、`active=false`；
  功能授权缺失视为关闭（账号级 `user_feature_auth` 覆盖团队级 `team_feature_auth`）。
- `team.status`：1 正常 / 2 停用；团队停用时 `featureAuth` 一律为 `false`（在线更新检查返回 `reason=team_disabled`）。
- `isSuperAdmin`：`privilege` 的 `Admin` 位或系统角色 `super_admin` 任一成立即为 `true`。

### 8.2 我的团队 (Get Team)
- **URL**: `/api/team`
- **Method**: `GET`，`Authorization: Bearer {token}`
- **Response**: `data = { team{id,name,roleCode}, subscription{startDate,endDate,active}, members[] }`，
  `members[]` 元素：`userId, userName, nickName, roleCode, roleName, status(1 正常/2 停用), mustChangePwd, createdAt`
- **错误码**: `-802` 用户不属于任何团队

### 8.3 团队管理员新建成员 (Create Member)
- **URL**: `/api/team/member`
- **Method**: `POST`，`Authorization: Bearer {token}`
- **Body**: `{ "userName": "...", "password": "...", "roleCode": "engineer|pm", "nickName": "..." }`
- **说明**: 仅团队管理员可调用；新账号 `must_change_pwd=true`（首次登录强制改密），并自动加入调用者所在团队、余额 0。
  不允许分配 `team_admin`（防止越权扩权）。
- **Response**: `data = { userId }`
- **错误码**: `-102` 参数缺失/密码过短、`-303` 用户名已存在、`-802` 不在团队、`-803` 非团队管理员、`-805` 角色不允许、`-804` 创建失败

### 8.4 厂商后台：团队列表 (Admin List Teams)
- **URL**: `/api/admin/teams?paid=1&expired=1`
- **Method**: `GET`，需要 `Admin` 权限
- **Response**: `data = { teams[]{ id, name, status, createdAt, subscription{startDate,endDate,active}, memberCount } }`
- **错误码**: `-807` 需要管理员权限（HTTP 403）

### 8.5 厂商后台：设置付费期 (Admin Set Subscription)
- **URL**: `/api/admin/teams/{teamId}/subscription`
- **Method**: `PUT`，需要 `Admin` 权限
- **Body**: `{ "startDate": "2026-10-26", "endDate": "2027-10-26", "plan": "standard" }`
- **说明**: 追加一条订阅记录（保留历史），当前生效记录取 `end_date` 最大且 `status=1` 的一条。
- **错误码**: `-801` 团队不存在、`-806` 日期不合法（格式或 start > end）

### 8.6 厂商后台：设置云端功能授权 (Admin Set Feature Auth)
- **URL**: `/api/admin/teams/{teamId}/feature-auth`
- **Method**: `PUT`，需要 `Admin` 权限
- **Body**: `{ "featureCode": "cloud_project|online_update", "enabled": true }`
- **说明**: 按 `(team_id, feature_code)` upsert；客户端 bootstrap 读取时账号级配置优先。
- **错误码**: `-801` 团队不存在、`-105` 未知功能代码

### 8.7 厂商后台：团队详情 (Admin Get Team)
- **URL**: `/api/admin/teams/{teamId}`
- **Method**: `GET`，需要 `Admin` 权限（`Admin` 位或系统角色 `super_admin`）
- **Response**:
```json
{
  "code": 0, "msg": "success",
  "data": {
    "team": {
      "id": 5, "name": "Team-5-acme", "status": 1,
      "ownerUserId": 12, "ownerName": "acme", "createdAt": "2026-10-26"
    },
    "subscription": { "startDate": "2026-10-26", "endDate": "2027-10-26", "active": true },
    "featureAuth": { "cloud_project": true, "online_update": true },
    "members": [
      { "userId": 12, "userName": "acme", "nickName": "acme", "roleCode": "team_admin",
        "roleName": "团队管理员", "status": 1, "mustChangePwd": false,
        "createdAt": "2026-10-26 10:00:00" }
    ],
    "memberCount": 1
  }
}
```
- 与 8.2 的区别：可指定任意团队，且额外返回创建者与团队状态。
- **错误码**: `-801` 团队不存在

### 8.8 厂商后台：团队改名 / 启停 (Admin Update Team)
- **URL**: `/api/admin/teams/{teamId}`
- **Method**: `PATCH`，需要 `Admin` 权限
- **Body**: `{ "name": "新团队名", "status": 1 }`（两个字段都可选，至少给一个；`status` 只能是 1/2）
- **说明**: 团队名全局唯一；停用后该团队的云端授权、在线更新检查与团队建号都会被拒绝。
- **错误码**: `-801` 团队不存在、`-815` 团队名已存在、`-105` 状态不合法、`-102` 未提供任何字段

### 8.9 厂商后台：修改成员角色 / 状态 (Admin Update Member)
- **URL**: `/api/admin/teams/{teamId}/members/{userId}`
- **Method**: `PATCH`，需要 `Admin` 权限
- **Body**: `{ "roleCode": "engineer|pm|team_admin|...", "status": 1 }`（两个字段都可选，至少给一个）
- **说明**:
  - `roleCode` 只接受团队角色（`role.scope = 2`），系统角色不能挂到团队成员位；
  - 团队创建者（`team.owner_user_id`）不允许被停用或降级为 `team_admin` 之外的角色，
    以保证团队始终有可管理的管理员（需换人时先转移团队所有权）。
- **Response**: `data = { member{...同 8.2 members 元素} }`
- **错误码**: `-801` 团队不存在、`-802` 账号不在该团队、`-805` 角色不存在或不可分配、
  `-818` 团队创建者不能被停用或降级、`-105` 状态不合法

### 8.10 厂商后台：账号检索 (Admin List Users)
- **URL**: `/api/admin/users?keyword={用户名或昵称}&limit=50`
- **Method**: `GET`，需要 `Admin` 权限
- **Response**:
```json
{
  "code": 0, "msg": "success",
  "data": { "users": [
    { "userId": 12, "userName": "acme", "nickName": "acme", "status": 1,
      "teamId": 5, "teamName": "Team-5-acme", "roleCode": "team_admin", "isSuperAdmin": false }
  ] }
}
```
- `keyword` 为空返回前 `limit` 个账号（`limit` 上限 200）。

### 8.11 厂商后台：授予 / 回收系统角色 (Admin Set System Role)
- **URL**: `/api/admin/users/{userId}/system-roles`
- **Method**: `POST`，调用者必须持有 **`Admin` 位**（仅有 `super_admin` 角色不够）
- **Body**: `{ "roleCode": "super_admin", "enabled": true }`
- **说明**: `super_admin` 是 `role.scope = 1` 的系统角色，经 `user_system_role` 直接挂到账号上，
  不占用团队里的唯一角色位；授予后该账号可调用 8.4–8.10 的全部接口。
  授予权只认 `Admin` 位，避免拿到角色的账号自行扩散；回填逻辑在迁移 `0003_super_admin.sql`。
- **Response**: `data = { userId, roleCode, enabled }`
- **错误码**: `-807` 调用者没有 `Admin` 位、`-101` 账号不存在、`-805` 角色不存在（目前只接受 `super_admin`）、
  `-102` 缺少 roleCode / enabled


## 9. 余额与支付（当前为模拟支付）

> 金额单位统一为 **分**（`bigint`），展示时除以 100。
> 本期为模拟支付：下单后调用 `mock-paid` 直接到账；真实支付宝当面付 / 微信 Native 接入时，
> 只需把订单的 `qr_content` 换成渠道返回的 `code_url`、把 `mock-paid` 换成渠道回调验签，
> 订单、流水、余额与幂等逻辑不变（回调建议先落 `payment_callback_log` 再入账）。

### 9.1 创建充值订单
- **URL**: `/api/bill/order`
- **Method**: `POST`，`Authorization: Bearer {token}`
- **Body**: `{ "amountCents": 10000, "channel": 9, "idemKey": "可选，客户端生成的幂等键" }`
  - `channel`: 1 支付宝 / 2 微信 / 9 模拟
  - `amountCents`: 1 ~ 10000000
- **Response**: `data = { orderNo, amountCents, channel, status(0 待支付), qrContent, qrPngUrl, expireAt }`
  - 同一 `idemKey` 重复下单返回原订单（不新建）
- **错误码**: `-903` 金额不合法、`-904` 渠道不支持、`-104` 数据库错误

### 9.2 查询订单（轮询）
- **URL**: `/api/bill/order/{orderNo}`
- **Method**: `GET`，`Authorization: Bearer {token}`
- **Response**: `data = { orderNo, amountCents, channel, status, paid, createdAt, paidAt, balanceAfterCents? }`
- **错误码**: `-901` 订单不存在或不属于当前用户

### 9.3 余额流水
- **URL**: `/api/bill/transactions?type=0&page=1&size=50`
- **Method**: `GET`，`Authorization: Bearer {token}`
- **Params**: `type`（0 全部 / 1 充值 / 2 消费 / 3 退款）、`page`、`size`（≤200）
- **Response**: `data = { total, page, size, items[]{ id, type, amountCents, balanceAfterCents, remark, createdAt } }`

### 9.4 模拟到账（仅模拟支付阶段）
- **URL**: `/api/bill/order/{orderNo}/mock-paid`
- **Method**: `POST`，`Authorization: Bearer {token}`
- **说明**: 需要 `TestMode.Enable=true` 或订单 `channel=9`；**幂等**，重复调用不会重复加余额。
  到账在同一条 SQL（CTE）内完成：订单置已支付 → 建余额行 → 加余额 → 记流水，并发安全。
- **Response**: `data = { orderNo, paid: true, amountCents, balanceAfterCents }`
- **错误码**: `-901` 订单不存在、`-902` 订单已关闭、`-905` 模拟支付未开启

### 9.5 二维码占位图
- **URL**: `/api/bill/qr/{orderNo}`
- **Method**: `GET`（无需登录）
- **Response**: `image/png`，240×240 占位图。真实接入后改为渲染渠道 `code_url` 的二维码。


## 10. 厂商后台：角色 / 权限 / 权限组（RBAC）

> 前缀 `/api/admin`，均需 `AuthFilter` + `AdminFilter`（`privilege` 的 `Admin` 位或系统角色 `super_admin`），
> 非管理员返回 HTTP 403 / `-807`。
> `role.code` 与 `permission.code` 创建后**不可修改**（服务端鉴权与客户端映射依赖 code）；
> `permissionIds` 一旦出现在请求体里即为**整组替换**（传 `[]` 表示清空）。

### 10.1 角色
| 接口 | 说明 |
|---|---|
| `GET /api/admin/roles` | `data.roles[] = { id, code, name, scope(1 系统/2 团队), builtin, permissionIds[] }` |
| `POST /api/admin/roles` | Body `{ code, name, scope? }`；重复 code → `-815` |
| `PATCH /api/admin/roles/{id}` | Body `{ name?, permissionIds? }`，至少提供一个；不存在的权限 id → `-812` |
| `DELETE /api/admin/roles/{id}` | 内置角色（super_admin/team_admin/engineer/pm）或 `scope=1` 的系统角色 → `-816`；仍被 `team_member` 引用 → `-814` |

- `super_admin`（超级管理员）是预置的系统角色：`scope=1`、`builtin=true`、默认拥有全部已登记权限，
  通过 8.11 授予账号后即可跨团队查看/修改数据；它由 `0003_super_admin.sql` 种入，不随普通 RBAC CRUD 删除。

### 10.2 权限
| 接口 | 说明 |
|---|---|
| `GET /api/admin/permissions` | `data.permissions[] = { id, code, module, action }` |
| `POST /api/admin/permissions` | Body `{ code, module, action }`；重复 code → `-815` |
| `PATCH /api/admin/permissions/{id}` | Body `{ module?, action? }`，至少提供一个 |
| `DELETE /api/admin/permissions/{id}` | 仍被角色或权限组引用 → `-814` |

### 10.3 权限组
| 接口 | 说明 |
|---|---|
| `GET /api/admin/permission-groups` | `data.groups[] = { id, name, permissionIds[] }` |
| `POST /api/admin/permission-groups` | Body `{ name, permissionIds? }`；重名 → `-815` |
| `PATCH /api/admin/permission-groups/{id}` | Body `{ name?, permissionIds? }` |
| `DELETE /api/admin/permission-groups/{id}` | 同时清空组内 `group_permission` |


## 11. 在线更新（安装包分发）

> 客户端只有在 **已登录 + 团队已付费 + 已开通 online_update 授权** 时才能看到/下载安装包。
> 安装包存放在 `custom_config.Installer.StoreDir`（默认 `./installers`），由本服务直出下载（前面可再挂 nginx）。
> 上传接口需要 `multipart/form-data`，服务端已把请求体上限放宽到 2GB，超过 8MB 的部分落临时文件。

### 11.1 检查更新
- **URL**: `/api/update/check?currentVersion=11.7.120&platform=win32`
- **Method**: `GET`，`Authorization: Bearer {token}`
- **Response**（`hasUpdate=false` 时也返回 200）：
```json
{ "code": 0, "msg": "success", "data": {
    "hasUpdate": true, "reason": "ok",
    "currentVersion": "11.7.120", "latestVersion": "11.7.121",
    "version": "11.7.121", "buildNo": 202610271350,
    "fileName": "UEAP_PRJ_11.7.121_Setup.exe", "sizeBytes": 123456789,
    "sha256": "…", "notes": "…", "publishedAt": "2026-10-26 17:00:00",
    "downloadUrl": "https://im.uesoft.com/api/update/download/11.7.121"
} }
```
- `reason` 取值：`ok` / `not_paid` / `expired`（曾付费但已过期）/ `no_auth`（未开通云端更新授权）/
  `no_package`（没有已发布安装包）/ `no_update`（已是最新）。除 `ok` 外一律 `hasUpdate=false`。
- 版本比较按 `a.b.c` 数字逐段比较。

### 11.2 下载安装包
- **URL**: `/api/update/download/{version}`
- **Method**: `GET`，`Authorization: Bearer {token}`
- **Response**: 二进制流，`Content-Disposition: attachment; filename="…"`
- **错误码**: `-808` 团队未付费、`-809` 未开通更新授权、`-817` 安装包不存在或未发布

### 11.3 厂商后台：安装包管理
| 接口 | 说明 |
|---|---|
| `POST /api/admin/installer/upload` | `multipart/form-data`：`version`(可空)、`buildNo`(可空)、`notes`、`minVersion`、`file`(必填, 仅 `.exe`)；落盘并计算 sha256，状态为**草稿(0)**；版本号重复 → `-815` |
| `PATCH /api/admin/installer/{id}` | 编辑版本信息：Body `{ "version": "...", "buildNo": 0, "notes": "", "minVersion": "" }`（四项整体覆盖，`version` 必填）。只改库里的元数据，磁盘文件与 `file_path` 不动，下载仍按该记录取文件 |
| `POST /api/admin/installer/{id}/publish` | 发布：把该条置为已发布(1)，**其余已发布版本自动转为下线(2)**，保证同一时刻只有一个现行版本 |
| `POST /api/admin/installer/{id}/offline` | 下线：状态置为 2（记录保留，不再对外提供更新） |
| `GET /api/admin/installer/list` | 列出全部安装包（含草稿/下线），字段：id/version/buildNo(64 位)/fileName/sizeBytes/sha256/notes/minVersion/status/createdAt/publishedAt |
- 状态：`0` 草稿 / `1` 已发布 / `2` 下线。
- `buildNo` 为 64 位整数；上传/编辑接口既接受 JSON number，也接受数字字符串。
- `version` 缺省时按文件名解析：`UEAP_PRJ_11.7.120_Setup.Win32.build-202610271350.exe`
  → `11.7.120`；`buildNo` 缺省时取 `build-` / `build_` 后的数字。`build_no` 已改为 `bigint`
  （迁移 `0006_installer_build_no_bigint.sql`），12 位时间戳无需截断，完整保存与返回。
  文件名不符合版本约定且未填 `version` → `-102`。
- 编辑时的错误码：`-102` 版本号为空或超长、`-105` 构建号为负、`-815` 版本号已存在、`-817` 安装包不存在。

### 11.4 分片上传（断点续传）

> 登录器新客户端使用本协议；旧的 `POST /api/admin/installer/upload`（multipart）保留兼容。
> 会话表 `installer_upload_session` 由迁移 `0007_installer_upload_session.sql` 创建；
> 临时文件在 `custom_config.Installer.UploadTempDir`（默认 `./uploads/installer-tmp`）。

**1）初始化会话**
- **URL**: `POST /api/admin/installer/upload/init`
- **Body**:
```json
{
  "version": "11.7.121",
  "buildNo": "202610271350",
  "notes": "",
  "minVersion": "11.7.120",
  "fileName": "UEAP_PRJ_11.7.121_Setup.Win32.build-202610271350.exe",
  "fileSize": 524288000,
  "fileSha256": "…64 hex…"
}
```
- **Response**: `data = { uploadId, offset, chunkSize, resumed, alreadyUploaded? }`
- 行为：
  - 同一 `version` 已存在且 sha256 相同 → `alreadyUploaded=true`，客户端直接当成功；
  - 同一管理员、同一 `fileSha256 + fileSize` 有未完成会话 → 返回原 `uploadId` 与已上传 `offset`；
  - 否则新建会话并返回 `offset=0`。
- 限制：单文件 ≤ 8GB；只接受 `.exe`；`fileSha256` 必须是 64 位十六进制。
- 错误：`-102` 参数缺失/版本超长、`-105` 大小/后缀/hash 不合法、`-815` 同版本已存在但 hash 不同。

**2）上传分片**
- **URL**: `POST /api/admin/installer/upload/{uploadId}/chunk?offset=N`
- **Body**: 原始二进制（`Content-Type: application/octet-stream`），单次 ≤ 8MB（客户端用 4MB）
- **Response**: `data = { offset }`，表示服务端当前已写入字节数。
- `offset` 必须等于服务端当前临时文件大小；不一致时不写入，直接返回真实 `offset` 让客户端重同步
  （用于“服务端已写入但响应丢失”的重试场景）。
- 错误：`-105` 会话不存在/已完成/分片越界、`-103` 写临时文件失败。

**3）完成**
- **URL**: `POST /api/admin/installer/upload/{uploadId}/complete`
- 校验临时文件大小与 sha256；一致后移动到 `Installer.StoreDir/<version>/<fileName>`，
  写入 `installer_version` 草稿（status=0），并将会话置为已完成（保留 7 天用于重复 complete 的幂等返回）。
- sha256 不一致 → 删除临时文件与会话，返回 `-105`，客户端需从 0 重传。
- 重复调用已完成会话 → 直接返回已有安装包信息。
- 错误：`-105` 未传完/校验失败/会话不存在、`-815` 同版本已被其它文件占用、`-103`/`-104` 落盘或数据库失败。

**清理**：每次 init 时清理 `updated_at` 超过 7 天的会话与临时文件（含已完成会话）。

> 注意：`main.cc` 的 `setClientMaxBodySize / setClientMaxMemoryBodySize` 必须在 `loadConfigFile` 之后设置；
> 否则配置默认的 1MB 上限会让分片/大请求在 90% 左右被断开（客户端表现为错误码 12031）。

### 11.5 上传页
- **URL**: `/admin/installer`（`GET`，无需登录即可打开页面，但页面内所有操作都要填厂商超管 token）
- 页面提供：粘贴 token → 加载列表 → 选 `.exe` 上传 → 发布。仅作内部工具，未做额外的会话管理。

## 12. 用户意见反馈

> 登录器「我的 → 意见反馈」使用。提交需要登录态；管理接口额外要求厂商超管（`AuthFilter + AdminFilter`）。
> 截图存放在 `custom_config.Feedback.StoreDir`（默认 `./uploads/feedback`），一条反馈最多 5 张，
> 库里 `user_feedback_image(feedback_id, seq, image_name, image_mime)` 保存每张的文件名。
> 建表迁移：`0005_user_feedback.sql` + `0008_feedback_multi_image.sql`（旧单图会回填为 `seq=0`）。

### 12.1 提交反馈
- **URL**: `/api/feedback`
- **Method**: `POST`，`Authorization: Bearer {token}`
- **Body**:
```json
{
  "category": 1,
  "content": "问题描述",
  "contact": "a@b.com",
  "appVersion": "11.7.120",
  "osInfo": "Windows 10/11 (x64)",
  "images": [
    { "mime": "image/png", "base64": "iVBORw0KGgo..." },
    { "mime": "image/jpeg", "base64": "/9j/4AAQSkZJRg..." }
  ]
}
```
- `category`：`0` 功能建议 / `1` 问题报错 / `2` 其他（缺省 0，越界按 2 处理）。
- `content` 必填（去空白后非空、≤ 20000 字节）；`contact` ≤ 200 字节；`appVersion/osInfo` ≤ 200 字节。
- `images` 可空，最多 5 张；每张只接受 `image/png` / `image/jpeg` / `image/bmp`，单张 ≤ 12MB、
  合计 ≤ 24MB，并校验文件头（PNG/JPEG/BMP）与格式一致。
- 兼容旧客户端：没有 `images` 数组时仍可传单张 `imageMime + imageBase64`。
- **Response**: `data = { feedbackId, categoryName, imageCount }`
- **错误码**：`-102` 内容为空 / `-105` 截图数量、格式或大小不合法 / `-104` 数据库或落盘失败。

### 12.2 反馈列表（厂商）
- **URL**: `/api/admin/feedback/list?status=-1&page=1&size=50`
- `status`：`-1` 全部（默认）/ `0` 待处理 / `1` 已处理；`size` 取值 1~200。
- **Response**: `data = { total, page, size, items[] }`，按 `id` 倒序。
- `items[]` 字段：`id/userId/userName/category/categoryName/content/contact/appVersion/osInfo/hasImage/imageCount/imageMime/status/createdAt/handledAt`。
- `imageCount` 为该反馈的截图张数（0~5）；`imageMime` 是第一张的 MIME。
- `userName` 是提交时的快照，用户改名/注销后仍能看出提交人。

### 12.3 读取截图（厂商）
- **URL**: `/api/admin/feedback/{id}/image?index=N`
- `index` 从 0 开始，取值 0~4；缺省为 0。
- **Response**: `data = { mime, base64, index }`
- **错误码**：`-105` 该反馈没有截图或序号不合法 / `-819` 反馈不存在 / `-103` 截图文件缺失。

### 12.4 标记已处理（厂商）
- **URL**: `/api/admin/feedback/{id}/handle`
- **Method**: `POST`
- 置 `status = 1`，同时记录 `handled_at = now()`、`handled_by = 当前管理员`。
- **错误码**：`-819` 反馈不存在。

> 登录器里的「导出 Markdown」在客户端本地完成（含截图下载到 `<文件名>_images/`），不新增服务端接口。

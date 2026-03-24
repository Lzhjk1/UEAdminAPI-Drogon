# API Error Codes

以下是各个接口可能返回的错误码（Code）、对应的枚举名称以及错误信息说明。

### 1. 登录模块 (Login Controller)

| 接口路径 (Method) | 错误码 (Code) | 枚举名称 (Enum) | 说明/默认信息 |
| :--- | :--- | :--- | :--- |
| **/user/login/pwd** (GET) | 0 | `ApiError_Success` | 登录成功 |
| | -102 | `ApiError_MissingRequiredArgs` | 缺少用户名或密码 |
| | -301 | `ApiError_InvalidCredentials` | 用户名或密码错误 |
| | -307 | `ApiError_PasswordNotSet` | 用户没有设置密码 |
| | -308 | `ApiError_UserUpdateFailed` | 更新状态失败 |
| **/user/login/email** (GET)<br>**/user/login/phone** (GET) | 0 | `ApiError_Success` | 登录成功 |
| | -302 | `ApiError_UserNotFound` | 用户不存在 |
| | -401 | `ApiError_InvalidVerifyCode` | 验证码错误 |
| | -308 | `ApiError_UserUpdateFailed` | 更新状态失败 |
| **/user/login/flashtoken** (GET) | 0 | `ApiError_Success` | 刷新成功 |
| | -204 | `ApiError_FlashTokenInvalidType` | 不是FlashToken |
| | -205 | `ApiError_FlashTokenVerificationFailed` | FlashToken验证失败 |
| | -206 | `ApiError_FlashTokenExpired` | FlashToken已失效 |
| **/user/token/verify** (GET)<br>**/user/self** (GET) | 0 | `ApiError_Success` | Token验证成功 / Success |
| | -101 | `ApiError_InvalidJsonFormat` | invalid token (格式错误) |
| | -203 | `ApiError_TokenMissing` | 缺少Token (仅限 /user/self) |
| | -202 | `ApiError_InvalidTokenType` | Token类型错误 (不是普通token) |
| | -207 | `ApiError_AuthenticationFailed` | 验证失败 |
| | -201 | `ApiError_TokenInvalidOrExpired` | Token已失效 |
| | -104 | `ApiError_DatabaseError` | 数据库错误 (仅限 /user/self) |

### 2. 注册模块 (Register Controller)

| 接口路径 (Method) | 错误码 (Code) | 枚举名称 (Enum) | 说明/默认信息 |
| :--- | :--- | :--- | :--- |
| **/user/create** (POST) | 0 | `ApiError_Success` | 创建用户成功 |
| | -101 | `ApiError_InvalidJsonFormat` | 请求体必须是JSON格式 |
| | -102 | `ApiError_MissingRequiredArgs` | 缺少必填项: ... |
| | -105 | `ApiError_InvalidOperation` | 权限参数错误 |
| | -401 | `ApiError_InvalidVerifyCode` | 验证码错误 |
| | -303 | `ApiError_UsernameAlreadyExists` | 用户名已存在 |
| | -304 | `ApiError_EmailAlreadyExists` | 邮箱已存在 |
| | -305 | `ApiError_PhoneAlreadyExists` | 手机号已存在 |
| | -312 | `ApiError_EmailAlreadyBound` | 该邮箱已绑定过账号 (第三方注册时) |
| | -502 | `ApiError_PlatformAlreadyBound` | 该平台已经绑定过账号 (第三方注册时) |
| | -309 | `ApiError_UserCreationFailure` | 创建用户失败 (回滚发生) |
| | -510 | `ApiError_ThirdPartyInfoCreationFailure` | 创建第三方登录信息失败 |
| | -601 | `ApiError_GitLabAccountCreationFailure` | 创建 GitLab 账号失败 |
| **/user/create/phone** (POST) | 0 | `ApiError_Success` | 注册成功 (返回 username, password) |
| | -102 | `ApiError_MissingRequiredArgs` | 缺少必填项: ... |
| | -401 | `ApiError_InvalidVerifyCode` | 验证码错误 |
| | -305 | `ApiError_PhoneAlreadyExists` | 手机号已存在 |
| | -601 | `ApiError_GitLabAccountCreationFailure` | 创建 GitLab 账号失败 |
| **/user/check_exist** (GET) | 0 | `ApiError_Success` | 用户已存在/不存在 |
| | -102 | `ApiError_MissingRequiredArgs` | 缺少参数: target |
| | -203 | `ApiError_TokenMissing` | 缺少Token |
| | -201 | `ApiError_TokenInvalidOrExpired` | Token已失效 |
| | -202 | `ApiError_InvalidTokenType` | Token类型错误 |

### 3. 用户管理模块 (User Controller)

| 接口路径 (Method) | 错误码 (Code) | 枚举名称 (Enum) | 说明/默认信息 |
| :--- | :--- | :--- | :--- |
| **/user/update** (POST) | 0 | `ApiError_Success` | 更新成功 |
| | -101 | `ApiError_InvalidJsonFormat` | 请求体必须是JSON格式 |
| | -102 | `ApiError_MissingRequiredArgs` | 缺少必要参数... |
| | -202 | `ApiError_InvalidTokenType` | 不是token |
| | -201 | `ApiError_TokenInvalidOrExpired` | Token已失效 |
| | -401 | `ApiError_InvalidVerifyCode` | 验证码错误 |
| | -302 | `ApiError_UserNotFound` | 用户不存在 |
| | -311 | `ApiError_ConcurrentModificationError` | 电话或邮箱不能同时修改 |
| | -306 | `ApiError_InvalidUsernameFormat` | 用户名格式不合法 |
| | -303 | `ApiError_UsernameAlreadyExists` | 用户名已存在 |
| | -103 | `ApiError_InternalError` | 内部错误 (更新Token状态失败) |
| | -105 | `ApiError_InvalidOperation` | 没有修改项 |
| | -308 | `ApiError_UserUpdateFailed` | 更新失败 |
| **/user/delete** (DELETE) | 0 | `ApiError_Success` | 删除成功 |
| | -102 | `ApiError_MissingRequiredArgs` | 缺少必填项... |
| | -202 | `ApiError_InvalidTokenType` | 不是token |
| | -201 | `ApiError_TokenInvalidOrExpired` | Token已失效 |
| | -302 | `ApiError_UserNotFound` | 用户不存在 |
| | -403 | `ApiError_TargetNotBoundToUser` | 目标未绑定到当前用户 |
| | -404 | `ApiError_UnknownChannelType` | 无法判断渠道类型 |
| | -401 | `ApiError_InvalidVerifyCode` | 验证码错误 |
| | -602 | `ApiError_GitLabAccountDeletionFailure` | 删除 GitLab 用户失败 |
| | -310 | `ApiError_UserDeletionFailure` | 删除失败 |

### 4. 验证码模块 (SendVerifyCode Controller)

| 接口路径 (Method) | 错误码 (Code) | 枚举名称 (Enum) | 说明/默认信息 |
| :--- | :--- | :--- | :--- |
| **/user/mfa** (GET) | 0 | `ApiError_Success` | 发送成功 |
| | -402 | `ApiError_SendVerifyCodeTooFrequent` | 发送过于频繁 |
| | -1 | (未定义枚举) | 发送失败 (如邮件服务不可用) |

> **注意**: `SendVerifyCode` 接口中仍存在硬编码的 `-1`，建议后续统一为 `ApiError_InternalError` 或新增 `ApiError_SendVerifyCodeFailed`。

### 5. 第三方登录模块 (ThirdPartyLogin Controller)

| 接口路径 (Method) | 错误码 (Code) | 枚举名称 (Enum) | 说明/默认信息 |
| :--- | :--- | :--- | :--- |
| **/api/third/authorization_url** (GET) | 0 | `ApiError_Success` | (返回URL数据) |
| | -501 | `ApiError_UnsupportedPlatform` | 不支持的第三方平台 / 请指定平台 |
| **/api/third/login/check** (GET)<br>**/api/third/login** (GET) | 0 | `ApiError_Success` | 验证成功 / 登录成功 |
| | -102 | `ApiError_MissingRequiredArgs` | 缺少必要参数 |
| | -501 | `ApiError_UnsupportedPlatform` | 不支持的第三方平台 |
| | -401 | `ApiError_InvalidVerifyCode` | 验证失败 |
| | -505 | `ApiError_LoginValueNotFound` | 找不到对应的登录值 |
| | -507 | `ApiError_CodeNotLoggedIn` | 该code尚未登录 |
| | -508 | `ApiError_LoginProcessing` | 登录请求处理中 |
| | -503 | `ApiError_PlatformNotBound` | 该平台未绑定过该账号 |
| | -103 | `ApiError_InternalError` | 内部错误 |
| **/api/third/bind** (POST) | 0 | `ApiError_Success` | 绑定成功 |
| | -102 | `ApiError_MissingRequiredArgs` | 缺少必要参数 |
| | -202 | `ApiError_InvalidTokenType` | 不是token |
| | -201 | `ApiError_TokenInvalidOrExpired` | Token已失效 |
| | -502 | `ApiError_PlatformAlreadyBound` | 该平台已经绑定过账号 |
| | -504 | `ApiError_ThirdPartyAuthFailed` | 第三方登陆验证失败 |
| | -103 | `ApiError_InternalError` | 内部错误 |
| | -401 | `ApiError_InvalidVerifyCode` | 验证码错误 |
| | -509 | `ApiError_BindingFailed` | 绑定第三方账号失败 |
| **/api/third/register** (POST) | 0 | `ApiError_Success` | (返回结果) |
| | -502 | `ApiError_PlatformAlreadyBound` | 该账号已经被绑定 |
| | -510 | `ApiError_ThirdPartyInfoCreationFailure` | 创建第三方登录信息失败 |
| | (其他) | (同 RegisterUser) | 继承注册接口的其他错误 |
| **/api/third/unbind** (POST) | 0 | `ApiError_Success` | 解绑成功 |
| | -102 | `ApiError_MissingRequiredArgs` | 缺少参数... |
| | -203 | `ApiError_TokenMissing` | 缺少参数 token |
| | -202 | `ApiError_InvalidTokenType` | 不是token |
| | -201 | `ApiError_TokenInvalidOrExpired` | Token已失效 |
| | -403 | `ApiError_TargetNotBoundToUser` | 目标未绑定到当前用户 |
| | -404 | `ApiError_UnknownChannelType` | 无法判断渠道类型 |
| | -401 | `ApiError_InvalidVerifyCode` | 验证码错误 |
| | -503 | `ApiError_PlatformNotBound` | (数据库中未找到绑定记录) |

### 6. GitLab 模块 (Gitlab Controller)

> **注意**: GitLab 模块目前**未使用**标准的 `ApiErrorCode` 枚举，而是直接返回 `{ "success": false, "message": "..." }` 的 JSON 格式。以下列出其可能返回的错误信息：

| 接口路径 (Method) | 状态 | 错误信息 (Message) |
| :--- | :--- | :--- |
| **所有 GitLab 接口** | `success: false` | 请求体必须是JSON格式 |
| | `success: false` | 缺少必填项: ... |
| **/api/gitlab/user/create** | `success: false` | 用户不存在 |
| | `success: false` | 创建GitLab用户失败 |
| | `success: false` | 保存GitLab用户信息失败 |
| **/api/gitlab/user/delete** | `success: false` | 用户GitLab信息不存在 |
| | `success: false` | 删除GitLab用户失败 |
| | `success: false` | 删除GitLab用户信息失败 |
| **/api/gitlab/user/password** | `success: false` | 用户GitLab信息不存在 |
| | `success: false` | 修改GitLab用户密码失败 |
| **/api/gitlab/user/token/create** | `success: false` | 用户GitLab信息不存在 |
| | `success: false` | 创建GitLab模拟令牌失败 |
| | `success: false` | 更新GitLab令牌信息失败 |
| **/api/gitlab/project/invite** | `success: false` | 用户GitLab信息不存在 |
| | `success: false` | 无效的访问级别 |
| | `success: false` | 邀请用户加入GitLab项目失败 |

### 7. 系统模块 (System Controller)

| 接口路径 (Method) | 错误码 (Code) | 枚举名称 (Enum) | 说明/默认信息 |
| :--- | :--- | :--- | :--- |
| **/system/ping** (GET) | 0 | `ApiError_Success` | Pong |
| | -104 | `ApiError_DatabaseError` | 数据库错误 |

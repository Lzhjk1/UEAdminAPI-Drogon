# API Error Codes

以下是各个接口可能返回的错误码（Code）、对应的枚举名称以及错误信息说明。

### 1. 登录模块 (Login Controller)

| 接口路径 (Method) | 错误码 (Code) | 枚举名称 (Enum) | 说明/默认信息 |
| :--- | :--- | :--- | :--- |
| **/api/user/login/pwd** (GET) | 0 | `ApiError_Success` | 登录成功 |
| | -102 | `ApiError_MissingRequiredArgs` | 缺少用户名或密码 |
| | -301 | `ApiError_InvalidCredentials` | 用户名或密码错误 |
| | -307 | `ApiError_PasswordNotSet` | 用户没有设置密码 |
| | -308 | `ApiError_UserUpdateFailed` | 更新状态失败 |
| **/api/user/login/email** (GET)<br>**/api/user/login/phone** (GET) | 0 | `ApiError_Success` | 登录成功 |
| | -302 | `ApiError_UserNotFound` | 用户不存在 |
| | -401 | `ApiError_InvalidVerifyCode` | 验证码错误 |
| | -401 | `ApiError_InvalidVerifyCode` | ActionToken 无效、已过期或无权执行此操作 |
| | -308 | `ApiError_UserUpdateFailed` | 更新状态失败 |
| **/api/user/login/flashtoken** (GET) | 0 | `ApiError_Success` | 刷新成功 |
| | -204 | `ApiError_FlashTokenInvalidType` | 不是FlashToken |
| | -205 | `ApiError_FlashTokenVerificationFailed` | FlashToken验证失败 |
| | -206 | `ApiError_FlashTokenExpired` | FlashToken已失效 |
| **/api/user/token/verify/flash** (GET) | 0 | `ApiError_Success` | FlashToken验证成功 |
| | -101 | `ApiError_InvalidJsonFormat` | invalid token (格式错误) |
| | -207 | `ApiError_AuthenticationFailed` | FlashToken验证失败 |
| **/api/user/token/verify/auth** (GET) | 0 | `ApiError_Success` | Token验证成功 |
| | -101 | `ApiError_InvalidJsonFormat` | invalid token (格式错误) |
| | -201 | `ApiError_TokenInvalidOrExpired` | 不是有效的 Token / Token已失效 |
| **/api/user/self** (GET) | 0 | `ApiError_Success` | Success |
| | -203 | `ApiError_TokenMissing` | 缺少Token |
| | -201 | `ApiError_TokenInvalidOrExpired` | Token已失效 |
| | -207 | `ApiError_AuthenticationFailed` | flashToken 不能直接用于认证 |
| | -207 | `ApiError_AuthenticationFailed` | 未知token类型 |
| | -104 | `ApiError_DatabaseError` | 数据库错误 |
| **/api/user/logout** (POST) | 0 | `ApiError_Success` | 注销成功 |
| | -103 | `ApiError_InternalError` | 内部错误 |
| | -201 | `ApiError_TokenInvalidOrExpired` | Token已失效 |
| | -202 | `ApiError_InvalidTokenType` | 无效的 Token |
| | -203 | `ApiError_TokenMissing` | 缺少Token |
| | -207 | `ApiError_AuthenticationFailed` | flashToken 不能直接用于认证 |
| | -207 | `ApiError_AuthenticationFailed` | 未知token类型 |
| | -308 | `ApiError_UserUpdateFailed` | 注销失败: 数据库更新错误 |

### 2. 注册模块 (Register Controller)

| 接口路径 (Method) | 错误码 (Code) | 枚举名称 (Enum) | 说明/默认信息 |
| :--- | :--- | :--- | :--- |
| **/api/user/create** (POST) | 0 | `ApiError_Success` | 创建用户成功 |
| | -101 | `ApiError_InvalidJsonFormat` | 请求体必须是JSON格式 |
| | -102 | `ApiError_MissingRequiredArgs` | 缺少必填项: ... |
| | -105 | `ApiError_InvalidOperation` | 权限参数错误 |
| | -401 | `ApiError_InvalidVerifyCode` | 验证码错误 |
| | -401 | `ApiError_InvalidVerifyCode` | ActionToken 无效、已过期或无权执行此操作 |
| | -303 | `ApiError_UsernameAlreadyExists` | 用户名已存在 |
| | -304 | `ApiError_EmailAlreadyExists` | 邮箱已存在 |
| | -305 | `ApiError_PhoneAlreadyExists` | 手机号已存在 |
| | -312 | `ApiError_EmailAlreadyBound` | 该邮箱已绑定过账号 (第三方注册时) |
| | -502 | `ApiError_PlatformAlreadyBound` | 该平台已经绑定过账号 (第三方注册时) |
| | -309 | `ApiError_UserCreationFailure` | 创建用户失败 (回滚发生) |
| | -510 | `ApiError_ThirdPartyInfoCreationFailure` | 创建第三方登录信息失败 |
| | -601 | `ApiError_GitLabAccountCreationFailure` | 创建 GitLab 账号失败 |
| | -603 | `ApiError_GitLabProjectInvitationFailure` | 邀请用户加入项目失败 |
| | -604 | `ApiError_GitLabTokenCreationFailure` | 创建 GitLab Impersonation Token失败 |
| **/api/user/create/phone** (POST)<br>**/api/user/create/email** (POST) | 0 | `ApiError_Success` | 注册成功 (返回 username, password) |
| | -102 | `ApiError_MissingRequiredArgs` | 缺少必填项: ... |
| | -401 | `ApiError_InvalidVerifyCode` | 验证码错误 |
| | -401 | `ApiError_InvalidVerifyCode` | ActionToken 无效、已过期或无权执行此操作 |
| | -304 | `ApiError_EmailAlreadyExists` | 邮箱已存在 |
| | -305 | `ApiError_PhoneAlreadyExists` | 手机号已存在 |
| | -601 | `ApiError_GitLabAccountCreationFailure` | 创建 GitLab 账号失败 |
| | -603 | `ApiError_GitLabProjectInvitationFailure` | 邀请用户加入项目失败 |
| | -604 | `ApiError_GitLabTokenCreationFailure` | 创建 GitLab Impersonation Token失败 |
| **/api/user/check_exist** (GET) | 0 | `ApiError_Success` | 用户已存在/不存在 |
| | -102 | `ApiError_MissingRequiredArgs` | 缺少参数: target |
| | -203 | `ApiError_TokenMissing` | 缺少Token |
| | -201 | `ApiError_TokenInvalidOrExpired` | Token已失效 |
| | -202 | `ApiError_InvalidTokenType` | Token类型错误 |
| | -207 | `ApiError_AuthenticationFailed` | flashToken 不能直接用于认证 |
| | -207 | `ApiError_AuthenticationFailed` | 未知token类型 |

### 3. 用户管理模块 (User Controller)

| 接口路径 (Method) | 错误码 (Code) | 枚举名称 (Enum) | 说明/默认信息 |
| :--- | :--- | :--- | :--- |
| **/api/user/update** (POST) | 0 | `ApiError_Success` | 更新成功 |
| | -101 | `ApiError_InvalidJsonFormat` | 请求体必须是JSON格式 |
| | -102 | `ApiError_MissingRequiredArgs` | 缺少必要参数... |
| | -202 | `ApiError_InvalidTokenType` | 不是token |
| | -201 | `ApiError_TokenInvalidOrExpired` | Token已失效 |
| | -401 | `ApiError_InvalidVerifyCode` | ActionToken 无效、已过期或无权执行此操作 |
| | -207 | `ApiError_AuthenticationFailed` | flashToken 不能直接用于认证 |
| | -207 | `ApiError_AuthenticationFailed` | 未知token类型 |
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
| | -401 | `ApiError_InvalidVerifyCode` | ActionToken 无效、已过期或无权执行此操作 |
| | -207 | `ApiError_AuthenticationFailed` | flashToken 不能直接用于认证 |
| | -207 | `ApiError_AuthenticationFailed` | 未知token类型 |
| | -302 | `ApiError_UserNotFound` | 用户不存在 |
| | -403 | `ApiError_TargetNotBoundToUser` | 目标未绑定到当前用户 |
| | -404 | `ApiError_UnknownChannelType` | 无法判断渠道类型 |
| | -401 | `ApiError_InvalidVerifyCode` | 验证码错误 |
| | -602 | `ApiError_GitLabAccountDeletionFailure` | 删除 GitLab 用户失败 |
| | -310 | `ApiError_UserDeletionFailure` | 删除失败 |
| **/api/user/action_token** (GET) | 0 | `ApiError_Success` | ActionToken 颁发成功 |
| | -102 | `ApiError_MissingRequiredArgs` | 缺少参数: mfaType / 需要提供 mfaCode 和 target |
| | -105 | `ApiError_InvalidOperation` | 未知的操作类别 (mfaType) |
| | -201 | `ApiError_TokenInvalidOrExpired` | Token已失效 |
| | -202 | `ApiError_InvalidTokenType` | 不是token |
| | -203 | `ApiError_TokenMissing` | 缺少Token |
| | -207 | `ApiError_AuthenticationFailed` | 未登录 |
| | -207 | `ApiError_AuthenticationFailed` | flashToken 不能直接用于认证 |
| | -207 | `ApiError_AuthenticationFailed` | 未知token类型 |
| | -302 | `ApiError_UserNotFound` | 用户不存在 |
| | -401 | `ApiError_InvalidVerifyCode` | 验证码错误 |
| **/api/user/action_token/anonymous** (GET) | 0 | `ApiError_Success` | ActionToken 颁发成功 |
| | -102 | `ApiError_MissingRequiredArgs` | 缺少参数: mfaType / 需要提供 mfaCode 和 target |
| | -105 | `ApiError_InvalidOperation` | 未知的操作类别 (mfaType) / 此操作类型不允许匿名申请 |
| | -401 | `ApiError_InvalidVerifyCode` | 验证码错误 |

### 4. 验证码模块 (SendVerifyCode Controller)

| 接口路径 (Method) | 错误码 (Code) | 枚举名称 (Enum) | 说明/默认信息 |
| :--- | :--- | :--- | :--- |
| **/api/user/mfa** (GET) | 0 | `ApiError_Success` | 发送成功 |
| | -402 | `ApiError_SendVerifyCodeTooFrequent` | 发送过于频繁 |

| **/api/user/mfa/check** (GET) | 0 | `ApiError_Success` | 验证码正确 |
| | -102 | `ApiError_MissingRequiredArgs` | 缺少参数 target, code 或 type |
| | -401 | `ApiError_InvalidVerifyCode` | 验证码错误 |

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
| | -401 | `ApiError_InvalidVerifyCode` | ActionToken 无效、已过期或无权执行此操作 |
| | -207 | `ApiError_AuthenticationFailed` | flashToken 不能直接用于认证 |
| | -207 | `ApiError_AuthenticationFailed` | 未知token类型 |
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
| | -401 | `ApiError_InvalidVerifyCode` | ActionToken 无效、已过期或无权执行此操作 |
| | -207 | `ApiError_AuthenticationFailed` | flashToken 不能直接用于认证 |
| | -207 | `ApiError_AuthenticationFailed` | 未知token类型 |
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

### 7. OAuth2 设备登录模块 (OAuth2 Device Login Controller)

| 接口路径 (Method) | 错误码 (Code) | 枚举名称 (Enum) | 说明/默认信息 |
| :--- | :--- | :--- | :--- |
| **/api/oauth2/login/start** (POST) | 0 | `ApiError_Success` | 创建会话成功 |
| | -703 | `ApiError_DeviceLoginRedirectUriNotAllowed` | redirect_uri 仅允许 http://127.0.0.1 / http://localhost / http://[::1] |
| | -103 | `ApiError_InternalError` | DeviceLoginSessionService 未初始化 |
| **/api/oauth2/login/check** (GET) | 0 | `ApiError_Success` | 未完成时返回 active:false；完成时返回 token/flashToken/username |
| | -701 | `ApiError_DeviceLoginStateInvalid` | state 不存在、已过期或已消费 |
| | -702 | `ApiError_DeviceLoginStateConsumed` | state 已被其他请求消费（含并发轮询抢占） |
| | -302 | `ApiError_UserNotFound` | 用户已被删除 |
| | -103 | `ApiError_InternalError` | 查询用户失败 |
| | -308 | `ApiError_UserUpdateFailed` | 更新失败 |
| **/login** (GET) | — | — | 渲染登录页成功，返回 HTML；不返回 JSON `{code:0}` |
| | -701 | `ApiError_DeviceLoginStateInvalid` | state 不存在或已过期 |
| **/login** (POST) | 0 | `ApiError_Success` | 登录成功并跳转通知 |
| | -101 | `ApiError_InvalidJsonFormat` | 请求体必须是JSON格式（Content-Type 为 application/json 但解析失败） |
| | -102 | `ApiError_MissingRequiredArgs` | 缺少必要参数 |
| | -701 | `ApiError_DeviceLoginStateInvalid` | state 不存在或已过期 |
| | -702 | `ApiError_DeviceLoginStateConsumed` | 该登录会话已完成，请直接使用 login/check 获取 token |
| | -703 | `ApiError_DeviceLoginRedirectUriNotAllowed` | redirect_uri 与登录会话不一致 |
| | -301 | `ApiError_InvalidCredentials` | 用户名或密码错误 |
| | 说明 | — | 支持 JSON 与 application/x-www-form-urlencoded 表单两种提交格式；表单提交失败时不返回 JSON，而是 302 回登录页 `/login?state=...&redirect_uri=...&error=<错误信息>` |
| **/api/third/authorization_url** (GET)<br>（带 ueadmin_state 的设备登录路径） | 0 | `ApiError_Success` | (返回授权URL数据) |
| | -501 | `ApiError_UnsupportedPlatform` | 不支持的第三方平台 |
| | -701 | `ApiError_DeviceLoginStateInvalid` | ueadmin_state 不存在、已过期或已消费 |

### 8. 系统模块 (System Controller)

| 接口路径 (Method) | 错误码 (Code) | 枚举名称 (Enum) | 说明/默认信息 |
| :--- | :--- | :--- | :--- |
| **/api/system/ping** (GET) | 0 | `ApiError_Success` | Pong |
| | -104 | `ApiError_DatabaseError` | 数据库错误 |
| | -105 | `ApiError_InvalidOperation` | 无效操作 |
| | -106 | `ApiError_TooManyRequests` | 访问过于频繁，请稍后再试 |


### 9. 账号中心 / 团队 / 授权模块 (-8xx)

| 接口路径 (Method) | 错误码 (Code) | 枚举名称 (Enum) | 说明/默认信息 |
| :--- | :--- | :--- | :--- |
| **/api/me/bootstrap** (GET) | 0 | `ApiError_Success` | 返回余额/团队/付费期/授权 |
| | -203 | `ApiError_TokenMissing` | 缺少 Authorization |
| | -302 | `ApiError_UserNotFound` | 用户不存在 |
| | -104 | `ApiError_DatabaseError` | 数据库错误 |
| **/api/team** (GET) | 0 | `ApiError_Success` | 团队 + 成员列表 |
| | -802 | `ApiError_NotTeamMember` | 用户不属于任何团队 |
| | -801 | `ApiError_TeamNotFound` | 团队不存在 |
| **/api/team/member** (POST) | 0 | `ApiError_Success` | `data.userId` |
| | -102 | `ApiError_MissingRequiredArgs` | 用户名/密码缺失或密码不足 6 位 |
| | -303 | `ApiError_UsernameAlreadyExists` | 用户名已存在 |
| | -802 | `ApiError_NotTeamMember` | 调用者不属于任何团队 |
| | -803 | `ApiError_NotTeamAdmin` | 只有团队管理员可以建号 |
| | -805 | `ApiError_InvalidRoleCode` | 角色不存在或不允许分配（team_admin） |
| | -804 | `ApiError_TeamMemberCreateFailure` | 创建失败（已回滚用户） |
| **/api/admin/teams** (GET)<br>**/api/admin/teams/{id}/subscription** (PUT)<br>**/api/admin/teams/{id}/feature-auth** (PUT) | 0 | `ApiError_Success` | 厂商后台 |
| | -807 | `ApiError_AdminPrivilegeRequired` | 需要管理员权限（HTTP 403） |
| | -801 | `ApiError_TeamNotFound` | 团队不存在 |
| | -806 | `ApiError_SubscriptionInvalid` | 付费起止日期不合法 |
| | -105 | `ApiError_InvalidOperation` | 未知功能代码 |
| | -104 | `ApiError_DatabaseError` | 数据库错误 |

### 9.1 超级管理员 / 跨团队 (-8xx)

| 接口路径 (Method) | 错误码 (Code) | 枚举名称 (Enum) | 说明/默认信息 |
| :--- | :--- | :--- | :--- |
| **/api/admin/teams/{id}** (GET/PATCH) | 0 | `ApiError_Success` | 团队详情 / 改名与启停 |
| | -801 | `ApiError_TeamNotFound` | 团队不存在 |
| | -815 | `ApiError_CodeAlreadyExists` | 团队名已存在 |
| | -105 | `ApiError_InvalidOperation` | status 不是 1/2 |
| **/api/admin/teams/{id}/members/{userId}** (PATCH) | 0 | `ApiError_Success` | 修改成员角色 / 状态 |
| | -802 | `ApiError_NotTeamMember` | 账号不在该团队 |
| | -805 | `ApiError_InvalidRoleCode` | 角色不存在或不是团队角色 |
| | -818 | `ApiError_TeamOwnerProtected` | 团队创建者不能被停用或降级 |
| **/api/admin/users** (GET) | 0 | `ApiError_Success` | 账号检索 |
| **/api/admin/users/{id}/system-roles** (POST) | 0 | `ApiError_Success` | 授予 / 回收 super_admin |
| | -302 | `ApiError_UserNotFound` | 账号不存在 |
| | -805 | `ApiError_InvalidRoleCode` | 目前只接受 super_admin |
| | -102 | `ApiError_MissingRequiredArgs` | 缺少 roleCode / enabled |
| 以上全部 | -807 | `ApiError_AdminPrivilegeRequired` | 需要 `Admin` 位或系统角色 `super_admin`（HTTP 403） |
| 其中 system-roles | -807 | `ApiError_AdminPrivilegeRequired` | 授予/回收只认 `Admin` 位，角色持有者不能扩散 |


### 10. 余额与支付模块 (-9xx)

| 接口路径 (Method) | 错误码 (Code) | 枚举名称 (Enum) | 说明/默认信息 |
| :--- | :--- | :--- | :--- |
| **/api/bill/order** (POST) | 0 | `ApiError_Success` | 返回订单与二维码地址 |
| | -903 | `ApiError_AmountInvalid` | 金额不在 1 ~ 10000000 分 |
| | -904 | `ApiError_PayChannelUnsupported` | 渠道不是 1/2/9 |
| | -101 | `ApiError_InvalidJsonFormat` | 请求体非 JSON |
| **/api/bill/order/{orderNo}** (GET) | 0 | `ApiError_Success` | 订单状态与余额 |
| | -901 | `ApiError_OrderNotFound` | 订单不存在或不属于当前用户 |
| **/api/bill/transactions** (GET) | 0 | `ApiError_Success` | 余额流水 |
| | -104 | `ApiError_DatabaseError` | 数据库错误 |
| **/api/bill/order/{orderNo}/mock-paid** (POST) | 0 | `ApiError_Success` | 已到账（重复调用幂等） |
| | -901 | `ApiError_OrderNotFound` | 订单不存在 |
| | -902 | `ApiError_OrderStateInvalid` | 订单已关闭 |
| | -905 | `ApiError_MockPayDisabled` | 模拟支付未开启（非 9 渠道且 TestMode 关闭） |


### 11. RBAC 模块 (-811 ~ -816)

| 接口路径 (Method) | 错误码 (Code) | 枚举名称 (Enum) | 说明/默认信息 |
| :--- | :--- | :--- | :--- |
| **/api/admin/roles** (GET/POST/PATCH/DELETE) | 0 | `ApiError_Success` | 角色读写 |
| | -811 | `ApiError_RoleNotFound` | 角色不存在 |
| | -812 | `ApiError_PermissionNotFound` | 权限不存在（permissionIds 里有无效 id） |
| | -814 | `ApiError_ResourceInUse` | 角色仍被团队成员使用 |
| | -815 | `ApiError_CodeAlreadyExists` | 角色 code 已存在 |
| | -816 | `ApiError_SystemRoleProtected` | 内置角色或系统角色（scope=1）不允许删除 |
| **/api/admin/permissions** (GET/POST/PATCH/DELETE) | -812 | `ApiError_PermissionNotFound` | 权限不存在 |
| | -814 | `ApiError_ResourceInUse` | 权限仍被角色或权限组引用 |
| | -815 | `ApiError_CodeAlreadyExists` | 权限 code 已存在 |
| **/api/admin/permission-groups** (GET/POST/PATCH/DELETE) | -813 | `ApiError_PermissionGroupNotFound` | 权限组不存在 |
| | -815 | `ApiError_CodeAlreadyExists` | 权限组名已存在 |
| 以上全部 | -807 | `ApiError_AdminPrivilegeRequired` | 需要管理员权限（HTTP 403） |
| | -102 | `ApiError_MissingRequiredArgs` | 缺少必填字段 / 字段类型不合法 |
| | -104 | `ApiError_DatabaseError` | 数据库错误 |


### 12. 在线更新模块

| 接口路径 (Method) | 错误码 (Code) | 枚举名称 (Enum) | 说明/默认信息 |
| :--- | :--- | :--- | :--- |
| **/api/update/check** (GET) | 0 | `ApiError_Success` | 业务结果看 `data.reason` |
| | -102 | `ApiError_MissingRequiredArgs` | 缺少 currentVersion |
| **/api/update/download/{version}** (GET) | 0 | `ApiError_Success` | 返回文件流 |
| | -808 | `ApiError_UserNotPaid` | 团队未付费或已过期 |
| | -809 | `ApiError_FeatureNotAuthorized` | 未开通 online_update 授权 |
| | -817 | `ApiError_InstallerNotFound` | 安装包不存在或未发布 |
| | -103 | `ApiError_InternalError` | 安装包文件在磁盘上缺失 |
| **/api/admin/installer/upload** (POST) | -102 | `ApiError_MissingRequiredArgs` | 缺少 version / file |
| | -815 | `ApiError_CodeAlreadyExists` | 版本号已存在 |
| | -105 | `ApiError_InvalidOperation` | 非 multipart 或不是 .exe |
| | -103 | `ApiError_InternalError` | 建目录/写文件失败 |
| **/api/admin/installer/{id}** (PATCH) | -102 | `ApiError_MissingRequiredArgs` | version 为空或超过 64 字符 |
| | -105 | `ApiError_InvalidOperation` | 构建号为负数 |
| | -815 | `ApiError_CodeAlreadyExists` | 版本号已存在 |
| | -817 | `ApiError_InstallerNotFound` | 安装包不存在 |
| **/api/admin/installer/{id}/offline** (POST) | -817 | `ApiError_InstallerNotFound` | 安装包不存在 |
| **/api/admin/installer/{id}/publish** (POST) | -817 | `ApiError_InstallerNotFound` | 安装包不存在 |
| 以上后台接口 | -807 | `ApiError_AdminPrivilegeRequired` | 需要管理员权限（HTTP 403） |

> `/api/update/check` 的业务拒绝不占用错误码：团队停用返回 `data.reason = team_disabled`，
> 未付费 `not_paid` / `expired`，未授权 `no_auth`，无安装包 `no_package`，已最新 `no_update`。

## 用户意见反馈

| 接口路径 (Method) | 错误码 (Code) | 枚举名称 (Enum) | 说明/默认信息 |
| :--- | :--- | :--- | :--- |
| **/api/feedback** (POST) | -102 | `ApiError_MissingRequiredArgs` | 内容为空 |
| | -105 | `ApiError_InvalidOperation` | 截图数量/总大小/MIME/文件头不合法，或联系信息过长 |
| | -104 | `ApiError_DatabaseError` | 数据库错误 |
| **/api/admin/feedback/list** (GET) | -807 | `ApiError_AdminPrivilegeRequired` | 需要厂商超级管理员 |
| **/api/admin/feedback/{id}/image** (GET) | -105 | `ApiError_InvalidOperation` | 该反馈没有截图或 index 不合法 |
| | -819 | `ApiError_FeedbackNotFound` | 反馈不存在 |
| **/api/admin/feedback/{id}/handle** (POST) | -819 | `ApiError_FeedbackNotFound` | 反馈不存在 |

> `-819 ApiError_FeedbackNotFound` 为本轮新增（`utils/ApiErrorCodes.h`）。

## 安装包分片上传（断点续传）

| 接口路径 (Method) | 错误码 (Code) | 枚举名称 (Enum) | 说明/默认信息 |
| :--- | :--- | :--- | :--- |
| **/api/admin/installer/upload/init** (POST) | -102 | `ApiError_MissingRequiredArgs` | version/fileName 缺失或版本超长 |
| | -105 | `ApiError_InvalidOperation` | 大小/后缀/sha256 不合法，或构建号为负 |
| | -815 | `ApiError_CodeAlreadyExists` | 同版本已存在但文件 sha256 不同 |
| **/api/admin/installer/upload/{id}/chunk** (POST) | -105 | `ApiError_InvalidOperation` | 会话不存在/已完成、分片越界 |
| | -103 | `ApiError_InternalError` | 写临时文件失败 |
| **/api/admin/installer/upload/{id}/complete** (POST) | -105 | `ApiError_InvalidOperation` | 未传完、sha256 不一致、会话不存在 |
| | -815 | `ApiError_CodeAlreadyExists` | 同版本号已被其它文件占用 |
| | -103 / -104 | `ApiError_InternalError` / `ApiError_DatabaseError` | 落盘或数据库失败 |
| 以上接口 | -807 | `ApiError_AdminPrivilegeRequired` | 需要厂商超级管理员 |

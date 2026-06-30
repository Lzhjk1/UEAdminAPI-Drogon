# ActionToken 架构说明

本服务端已不再承担用户管理相关职责，原 ActionToken 相关 HTTP 接口已取消注册。

当前状态:

- `/user/action_token` 已取消注册。
- `/user/action_token/anonymous` 已取消注册。
- 用户登录、注册、验证码、第三方登录、用户资料维护等依赖 ActionToken 的接口已取消注册。
- 当前保留的鉴权方式为 JWT，通过 `AuthFilter` 校验 `Authorization: Bearer <jwt>`。

历史实现中的 `ActionTokenService`、`ActionTokenFilter`、`ActionTokenMiddleware` 代码仍可能存在于工程中，但不再作为当前已注册 HTTP 接口的安全边界。

后续如重新引入高危业务操作，应优先基于新的业务边界重新评估是否需要 ActionToken，避免沿用旧用户中心语义。

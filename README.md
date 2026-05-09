# UEAdminAPI-drogon

## 项目简介
UEAdminAPI-drogon 是一个基于 [Drogon](https://github.com/drogonframework/drogon) C++ web 框架开发的高性能后台管理 API 服务. 项目采用 C++20 协程特性（`co_await`, `co_return`），底层使用 PostgreSQL 作为关系型数据库，所有数据库操作均通过 Drogon ORM 完成. 

该项目主要提供完备的用户中心服务，包含多方式注册登录、多因素认证（MFA）、第三方授权登录（OAuth/GitLab）以及基于安全令牌（ActionToken）的敏感操作防护机制. 

## 核心功能特性

- **多方式注册与登录**：支持通过邮箱、手机号、密码等传统方式注册和登录. 
- **第三方与 OAuth 登录**：支持 QQ 和 WeChat 第三方平台登录. 
- **多因素认证（MFA）**：
  - 完善的 MFA 机制支持（邮箱验证码、短信验证码）. 
  - 支持 SMTP 邮件发送插件以及腾讯云 SMS 短信服务. 
- **安全架构**：
  - Token, FlashToken, ActionToken三种安全令牌类型. 分别用于一般接口认证, 刷新 Token, 敏感操作.
  - Token : 用于一般接口认证, 包含用户id(*暴露实际的顺序id并不合适, 计划之后修改*), 有效期短. 
  - FlashToken : 用于刷新 Token, 有效期较长. 
  - ActionToken : 用于敏感操作, 包含 Opaque Token（随机字符串）的单次有效状态机防护, 如果请求在业务上失败, 部分接口会有恢复操作, 不会使 Token 失效. 
- **速率限制**：内置 `RateLimitFilter` 防止接口被恶意刷量, 暂未测试使用. 

## 技术栈与依赖

- **Web 框架**：[Drogon](https://github.com/drogonframework/drogon) (C++14/17/20, 本项目深度使用 C++20 协程)
- **数据库**：PostgreSQL (配合 Drogon 的 `drogon::orm`)
- **JSON 处理**：JsonCPP (Drogon 内置)
- **第三方库集成**：
  - [jwt-cpp](https://github.com/Thalhammer/jwt-cpp)（用于 JSON Web Token 的生成与解析）
- **编译工具**：CMake

## 项目结构概览

- `controllers/`：API 路由控制器层. 负责定义接口路径、读取参数（Body内参数统一使用 `PostParamMap`）、并调用对应 Service，保持逻辑轻量. 
- `services/`：核心业务逻辑层（Service 层）. 包含 `AuthService`、`ActionTokenService`、`GitlabService` 等核心单例服务. 
- `models/`：数据库 ORM 模型定义，由 `drogon_ctl` 工具根据数据库结构自动生成. 
- `filters/`：请求拦截器和中间件. 包含身份验证（`AuthFilter`）、敏感操作验证（`ActionTokenFilter`）以及限流（`RateLimitFilter`）. 
- `plugins/`：Drogon 插件扩展，如 `SMTPMail` 邮件发送插件. 
- `utils/`：通用工具类，包含 API 错误码定义、数据格式化、随机数生成、单例基类等. 
- `docs/`：项目相关说明文档，包含 API 参考、架构设计等. 
- `swagger/`：API 文档生成脚本及静态页面. 
- `test/` & `testScript/`：单元测试与 Python 接口自动化测试脚本. 

## 开发规范与指南

本项目目前有以下规范：

1. **Service 层业务实现**：控制器 (`controllers`) 中不应包含具体的业务代码，只负责参数校验和响应封装，实际业务必须下沉至对应的服务类 (`services`). 
2. **全局单例服务获取**：所有服务单例（如 `auto _authService = AuthService::Instance();`）应在函数开头统一获取. 单例需遵循 `SingletonWithInit` 规范，并在构造函数结束时记录初始化日志 (`LOG_INFO << "{ServiceName} 初始化完成";`). 
3. **C++20 协程规范**：在使用 `drogon::Task` 返回值的协程函数中，必须使用 `co_return` 而不是 `return`，并注意跨 `co_await` 的锁生命周期管理，避免死锁. 
4. **数据库操作**：不直接拼接或编写 SQL 语句，所有数据库交互通过 `models/` 目录下的 ORM 类实现, 具体参见 drogon 文档的 ORM 章节. 
5. **接口安全与授权**：
   - 常规需要登录的接口请添加 `AuthFilter`. 
   - 涉及账号安全的高危敏感操作（如删除用户、解绑邮箱），必须添加 `ActionTokenFilter` 或 `ActionTokenMiddleware`，并遵循 [ActionToken 架构](docs/ActionToken_Architecture.md). 
6. **文档同步更新**：新增或修改接口后，同步更新：
   - `docs/API_Error_Codes.md` 中的错误码说明. 
   - `utils/ApiErrorCodes.h` 中的错误码枚举值. 

> 更多开发与架构细节，请参考 `docs/` 目录下的文档说明. 

## 编译与运行指南

1. 首先参照 drogon 文档配置 vcpkg 环境，确保所有依赖项已安装. 如 `openssl`, `libpq`, `jwt-cpp`, `drogon`, `trantor` 等. 具体见 drogon 文档.
2. 依据项目根目录的 `config.yaml` 配置数据库连接、JWT 密钥、SMTP 及短信服务凭证等必要参数.

## 许可证
暂无

# UEAdminAPI-drogon

OAuth2 授权服务器 & 用户中心 API，基于 [Drogon](https://github.com/drogonframework/drogon) C++ Web 框架，内核使用 PostgreSQL + Drogon ORM。

## 核心功能

### 认证与授权
- **OAuth2 标准端点**：`/.well-known/jwks.json`（JWKS 公钥）、`/api/oauth2/introspect`（Token 验证）、`/api/oauth2/revoke`（Token 吊销）
- **多方式注册登录**：支持邮箱、手机号、密码登录，以及邮箱/手机验证码登录
- **Token 体系**：Token（短效认证）、FlashToken（长效刷新）、ActionToken（单次有效敏感操作防护）
- **多因素认证（MFA）**：邮箱验证码、腾讯云短信验证码

### 安全
- **非对称签名**：RS256 优先签名，HS512 fallback；支持自动生成密钥对
- **敏感操作防护**：ActionToken 状态机机制，防止重放攻击
- **请求限流**：内置 `RateLimitFilter`

### 集成
- **第三方登录**：QQ、微信 OAuth 接入
- **GitLab 集成**：注册时自动创建 GitLab 账号并授权

## 技术栈

| 组件 | 选型 |
|------|------|
| Web 框架 | Drogon（C++20 协程） |
| 数据库 | PostgreSQL + Drogon ORM |
| JWT | jwt-cpp（RS256 + HS512） |
| 邮件 | SMTP（插件化） |
| 短信 | 腾讯云 SMS（HTTP API） |

## 项目结构

```
controllers/  — 路由层（仅参数读取 + 调用 Service）
services/     — 业务逻辑层（单例服务）
models/       — ORM 模型（drogon_ctl 自动生成）
filters/      — 拦截器/中间件（AuthFilter、ActionTokenFilter、RateLimitFilter）
plugins/      — 插件（SMTPMail 等）
utils/        — 通用工具（错误码、格式化、单例基类）
docs/         — 架构文档
swagger/      — Swagger API 文档
```

## 快速开始

### 编译环境

推荐使用 Docker 编译（预装 GCC 16 + vcpkg + 全部依赖）：

```bash
# 使用预构建的编译镜像
docker run --rm \
  -v $(pwd):/mnt/project/ueadmin-api-drogon \
  ueadmin-drogon-build:0.1 \
  bash -c "cd /mnt/project/ueadmin-api-drogon && rm -rf build && mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j6"
```

### 配置

1. 复制 `config.yml` 并按需填写配置参数（数据库、JWT 密钥、SMTP、短信等）
2. 确保 PostgreSQL 服务已启动并创建对应数据库

### Docker 运行

```bash
docker-compose -f build_docker/docker-compose.yml up
```

## API 路由

### OAuth2 端点
| 路径 | 说明 |
|------|------|
| `/.well-known/jwks.json` | RSA 公钥 JWKS |
| `/api/oauth2/introspect` | Token 验证 |
| `/api/oauth2/revoke` | Token 吊销 |

### 认证
| 路径 | 说明 |
|------|------|
| `/api/user/login/password` | 密码登录 |
| `/api/user/login/email` | 邮箱验证码登录 |
| `/api/user/login/phone` | 手机验证码登录 |
| `/api/user/login/flashtoken` | FlashToken 刷新 |
| `/api/user/register` | 用户注册 |
| `/api/user/logout` | 注销 |

### 用户管理
| 路径 | 说明 |
|------|------|
| `/api/user/info` | 获取个人信息 |
| `/api/user/update` | 修改个人信息 |
| `/api/user/delete` | 删除账号 |

### MFA
| 路径 | 说明 |
|------|------|
| `/api/mfa/send_code` | 发送验证码 |
| `/api/user/action_token` | 获取 ActionToken |

## 许可证

MIT

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
build_docker/ — Docker 构建配置
controllers/  — 路由层（仅参数读取 + 调用 Service）
services/     — 业务逻辑层（单例服务）
models/       — ORM 模型（drogon_ctl 自动生成）
filters/      — 拦截器/中间件（AuthFilter、ActionTokenFilter、RateLimitFilter）
plugins/      — 插件（SMTPMail 等）
utils/        — 通用工具类等（错误码、格式化、单例基类）
docs/         — 架构文档
tests/        — 测试工具 (pytest)
```

## 快速开始

### 编译环境

本项目通过 GitHub Actions 使用 Docker 容器进行编译（CI 配置见 `.github/workflows/build.yml`）。CI 推送代码到任意分支时即会触发构建，包含 `build-linux` 与 `build-windows` 两个作业。

**Linux 构建（容器化）**

1. 利用 `docker/setup-buildx-action` 配置 Buildx，并开启 GHA 缓存（`cache-from`/`cache-to: type=gha`）。
2. 基于 [build_docker/Dockerfile.build](file:///d:/vc/UEAdminAPI_drogon_myOwn/build_docker/Dockerfile.build) 构建编译镜像 `ueadmin-api-drogon-build:ci`，镜像内预装 GCC 16 及 vcpkg 工具链。
3. 以该镜像运行容器挂载工作区进行配置与编译：

   ```bash
   docker run --rm \
     -v "${GITHUB_WORKSPACE}:/mnt/project/ueadmin-api-drogon" \
     -w /mnt/project/ueadmin-api-drogon \
     ueadmin-api-drogon-build:ci \
     bash -lc '
       cmake . \
         -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
         -G Ninja \
         -B build \
         -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_C_COMPILER=gcc-16 \
         -DCMAKE_CXX_COMPILER=g++-16 && \
       cmake --build build --parallel
     '
   ```

4. 编译产物随后用于构建运行镜像 [build_docker/Dockerfile.run](file:///d:/vc/UEAdminAPI_drogon_myOwn/build_docker/Dockerfile.run)，打上 `latest` 与 commit short sha 标签后推送至 DockerHub（需配置 `DOCKERHUB_USERNAME`/`DOCKERHUB_TOKEN`/`DOCKERHUB_REPO`）。
5. 二进制与运行镜像分别作为 artifact 上传（`UEAdminAPI_drogon-linux`、`UEAdminAPI_drogon-run-ci-image`）。

**Windows 构建**

在 `windows-latest` runner 上本地安装 vcpkg（锁定 `VCPKG_TAG`，使用 `actions/cache` 缓存 `C:\vcpkg`），用 MSVC 生成 x64 Release 产物并上传 artifact `UEAdminAPI_drogon-windows`。

> 本地编译可参照上述 CI 流程：使用 `build_docker/Dockerfile.build` 构建镜像后同样以容器方式执行 cmake/ninja 构建。

### 配置

1. 复制 `config.template.yml` 为 `config.yml` 并按需填写配置参数（数据库、JWT 密钥、SMTP、短信等）
2. 确保 PostgreSQL 服务已启动并创建对应数据库

### Docker 运行

```bash
docker-compose -f build_docker/docker-compose.yml up
```

## API 路由

详情见 [API Reference Documentation](<docs/API Reference Documentation.md>)

## 许可证

MIT

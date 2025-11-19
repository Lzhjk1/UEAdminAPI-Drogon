# UEAdminAPI-drogon

## 项目简介
UEAdminAPI 是一个基于 Drogon 框架开发的管理 API 服务，主要用于 GitLab 用户管理、第三方登录、多因素认证（MFA）等功能。该项目实现了与 GitLab API 的集成，并提供了注册、登录、用户邀请加入项目等核心功能。

### 主要功能
- **GitLab 用户管理**：创建、删除、修改用户密码、生成和删除模拟令牌。
- **用户邀请加入项目**：管理员可以邀请用户加入 GitLab 项目。
- **多因素认证（MFA）**：支持通过电子邮件和短信发送验证码。
- **第三方登录**：支持多种平台的第三方登录。
- **注册与登录**：支持通过邮箱、手机号、密码等方式注册和登录。

## 安装指南
该项目依赖 Drogon 框架和一些第三方库（如 JWT、Tencent SDK）。请确保已安装以下依赖：
- Drogon 框架
- jwt-cpp 库
- Tencent SDK（用于短信服务）

### 编译项目
1. 确保已安装 CMake 和编译工具链。
2. 在项目根目录下运行以下命令：
   ```bash
   mkdir build && cd build
   cmake ..
   make
   ```

## 使用示例
### 示例1: 初始化单例服务
```cpp
// 初始化 ExampleService
ExampleService::Init("Example", 100);
auto service = ExampleService::Instance();
```

### 示例2: 注册新用户
```cpp
// 发送注册请求
auto req = HttpRequest::newHttpRequest();
req->setMethod(Post);
req->setPath("/api/register");

Json::Value reqJson;
reqJson["username"] = "new_user";
reqJson["password"] = "password123";
reqJson["email"] = "user@example.com";
reqJson["verifyCode"] = "123456";

req->setBody(reqJson.toStyledString());

// 发送请求并处理响应
auto resp = co_await Register::RegisterUser(req);
```

### 示例3: 邀请用户加入项目
```cpp
// 发送邀请请求
auto req = HttpRequest::newHttpRequest();
req->setMethod(Post);
req->setPath("/api/gitlab/project/invite");

Json::Value reqJson;
reqJson["userId"] = "123";
reqJson["projectId"] = "456";
reqJson["accessLevel"] = "30"; // 访问级别

req->setBody(reqJson.toStyledString());

// 发送请求并处理响应
auto resp = co_await GitlabController::inviteToProject(req);
```

## 贡献指南
该项目欢迎社区贡献。请遵循以下步骤：
1. Fork 项目仓库。
2. 创建新分支并进行修改。
3. 提交 Pull Request 并描述修改内容。

## 许可证
本项目使用 MIT 许可证。详细信息请参见 [LICENSE](LICENSE/LICENSE-SMTPMail-drogon) 文件。

## 文档
- [SingletonWithInit 使用指南](docs/SingletonWithInitUsage.md)
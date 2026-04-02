# UEAdminAPI 测试脚本使用说明

`api_client.py` 是一个用于测试 `UEAdminAPI` 接口的 Python 命令行工具。它会自动处理 `Token` 和 `ActionToken`，并将登录态保存在本地的 `session.json` 文件中，方便您进行多步骤的接口测试。

## 环境准备

在使用脚本前，请确保您已经安装了 `requests` 库：

```bash
pip install requests
```

## 核心机制

脚本会在当前目录下生成一个 `session.json` 文件。
当您成功调用需要返回 `Token` 或 `ActionToken` 的接口时，脚本会自动将它们保存到 `session.json` 中，并在后续的请求中自动附加对应的 `Authorization` 和 `X-Action-Token` 请求头。

## 支持的命令列表

脚本通过传递不同的 `command` 参数来调用不同的接口：

```bash
python api_client.py <command> [options...]
```

| 命令 (command) | 描述 | 常用参数 |
| :--- | :--- | :--- |
| `ping` | 测试服务器连通性 | 无 |
| `mfa` | 发送 MFA 验证码 | `--target` (邮箱/手机号), `--type` (MFA类型) |
| `action_token_anon`| 未登录状态下获取 ActionToken | `--target`, `--type`, `--code` (验证码) |
| `action_token` | 登录状态下获取 ActionToken | `--target`, `--type`, `--code` (验证码) |
| `login_email` | 邮箱登录 | `--target` (邮箱地址) |
| `login_phone` | 手机号登录 | `--target` (手机号) |
| `register_phone` | 手机号快速注册 | `--target` (手机号) |
| `self` | 获取当前登录的个人信息 | 无 |
| `third_auth` | 获取第三方授权登录链接 | `--platform` (平台: qq, wechat) |
| `third_check` | 检查第三方登录授权状态 | `--platform` |
| `third_bind` | 将第三方账号绑定至当前登录账号| `--platform` |
| `third_register` | 使用第三方账号直接注册并登录| `--platform` |

## 常见测试流程示例

### 1. 邮箱登录流程

**步骤 1：发送登录验证码**
```bash
python api_client.py mfa --target your_email@example.com --type Login
```

**步骤 2：获取匿名 ActionToken**
查看您的邮箱，获取验证码（例如：123456），然后运行：
```bash
python api_client.py action_token_anon --target your_email@example.com --type Login --code 123456
```
*(成功后，`ActionToken` 会自动保存在 `session.json` 中)*

**步骤 3：执行登录**
```bash
python api_client.py login_email --target your_email@example.com
```
*(成功后，登录凭证 `Token` 会自动保存在 `session.json` 中)*

**步骤 4：验证登录态（获取个人信息）**
```bash
python api_client.py self
```

---

### 2. 第三方账号绑定流程 (例如 QQ)

*(前提：您已经完成了上述的邮箱或手机登录流程，拥有有效的 `Token`)*

**步骤 1：申请高危操作的 ActionToken (类型：ThirdPartyBind)**
首先，给自己发送绑定用的验证码：
```bash
python api_client.py mfa --target your_email@example.com --type ThirdPartyBind
```
收到验证码后，申请 ActionToken：
```bash
python api_client.py action_token --target your_email@example.com --type ThirdPartyBind --code 123456
```

**步骤 2：获取第三方授权链接**
```bash
python api_client.py third_auth --platform qq
```
*(脚本会输出一个 `authorizationUrl`，请在浏览器中打开并完成扫码授权)*

**步骤 3：完成绑定**
扫码完成后，执行绑定操作：
```bash
python api_client.py third_bind --platform qq
```

**步骤 4：查看是否绑定成功**
```bash
python api_client.py self
```
*(在返回的 JSON 中，您应该能在 `configured_third_platform_name` 列表中看到绑定的 QQ 信息)*

## 注意事项
1. `session.json` 中包含了您的敏感凭证，请不要将其提交到代码仓库中（本项目已配置 `.gitignore`，一般会自动忽略该文件，但建议注意防范）。
2. 如果您的 ActionToken 提示过期（默认有效期 5 分钟），请重新从发送 `mfa` 验证码开始流程。
3. `ThirdPartyBind` 等敏感操作必须在拥有登录凭证 (`Token`) 的前提下进行。

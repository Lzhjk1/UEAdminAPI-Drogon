# UEADMIN 设备登录（RFC 8628）对接说明

> 替代旧 `state` 流程。旧流程把 `state` 同时放在浏览器 URL 和 `login/check` 里，
> 拿到 URL/日志的人可以在用户登录后领走 token；新流程把“浏览器短码”和
> “客户端秘密”拆开。

## 1. 发起授权

`POST /api/oauth2/device_authorization`

```json
{ "redirect_uri": "http://127.0.0.1:45678/cb" }
```

`redirect_uri` 只允许 loopback（`http://127.0.0.1` / `localhost` / `[::1]`），可为空。

响应：

```json
{
  "code": 0,
  "msg": "Success",
  "data": {
    "device_code": "8e3d...",
    "user_code": "ECCC-AEGF",
    "verification_uri": "https://im.uesoft.com/device",
    "verification_uri_complete": "https://im.uesoft.com/device?user_code=ECCC-AEGF",
    "expires_in": 600,
    "interval": 5
  }
}
```

- `device_code`：**只保存在客户端**，不能放进浏览器、日志或二维码。轮询 token 必须用它。
- `user_code`：给用户输入的 8 位短码，不是取 token 的凭证。
- `verification_uri`：让用户打开并输入短码；`verification_uri_complete` 是带短码的便捷链接。
- `/api/oauth2/login/start` 是同一接口的旧路径别名，返回新字段，不再返回 `state`。

## 2. 浏览器授权

用户打开 `verification_uri`，输入 `user_code`；服务端校验后显示登录页。用户可以用：

- 账号密码；
- 页内第三方扫码/跳转（`/api/third/authorization_url?...&user_code=...`）；
- 其他现有登录方式。

浏览器只负责“标记该设备会话已授权”，**任何 token 都不会出现在浏览器 URL/页面里**。
`/device` 是新验证页；旧 `/login?state=` 只把参数当 `user_code` 解析，旧 secret state 不再有效。

## 3. 客户端领取 token

客户端按 `interval` 轮询：

`POST /api/oauth2/token`

```json
{
  "grant_type": "urn:ietf:params:oauth:grant-type:device_code",
  "device_code": "8e3d..."
}
```

- 未授权：HTTP 400 `{"error":"authorization_pending"}`
- 轮询过快：HTTP 400 `{"error":"slow_down","interval":10}`（按新 interval 重试）
- 过期/已使用：HTTP 400 `{"error":"expired_token"}`
- 成功：HTTP 200

```json
{
  "access_token": "...",
  "token_type": "Bearer",
  "expires_in": 720,
  "refresh_token": "...",
  "token": "...",
  "flashToken": "...",
  "id": 123,
  "username": "alice"
}
```

兼容字段 `token/flashToken/id/username` 供现有客户端复用；`device_code` 只能成功换一次。

## 4. Pidgin / 客户端改造清单

旧代码（`GET /api/oauth2/login/start` + `GET /api/oauth2/login/check?state=`）需要改成：

1. 调 `/api/oauth2/device_authorization`，保存 `device_code`，显示 `user_code` 和 `verification_uri`。
2. 打开 `verification_uri`（建议不要直接把 `verification_uri_complete` 作为链接发给别人；让用户核对短码）。
3. 按 `interval` 用 `POST /api/oauth2/token` 轮询，处理 `authorization_pending` / `slow_down` / `expired_token`。
4. 成功后用返回的 `token` / `flashToken` 登录；不要再调用旧的 `login/check` GET。
5. 浏览器登录完成的 loopback 通知只表示“授权完成”，客户端仍需用 `device_code` 拉取 token。
6. 失败/超时后重新 `device_authorization`，旧 `device_code` 立即作废。

## 5. 安全边界

- 只拿到 `user_code` 不能换 token；token 只给持有 `device_code` 的客户端。
- 第三方回调只使用服务端保存的 `loginValue -> user_code` 映射，不再信任回调 query 里的 `ueadmin_state`。
- 这是一套标准 device flow，仍无法阻止用户被诱导输入攻击者提供的 `user_code`；验证页会显示“非本人发起请取消”，客户端应展示本次登录信息并要求用户确认。
- 生产必须 HTTPS；`device_code` 只存内存/受保护存储，不要写日志。

## 6. 回归验证

```bash
UEADMINAPI_BASE_URL=http://127.0.0.1:6666 \
UEADMINAPI_TEST_DOMAIN=example.com \
python3 -m pytest tests/test_oauth2_device_login.py -q
```

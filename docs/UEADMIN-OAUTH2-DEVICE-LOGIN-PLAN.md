# UEAdmin 侧实现计划：OAuth2 设备登录（Pidgin 支持）

> 对应设计文档：`D:\vc\unrealircd\doc\UEADMIN-OAUTH2-IRC-DESIGN.md` 4.1 节  
> 目标仓库：`D:\vc\UEAdminAPI_drogon_myOwn`

## 现状复核结论

- ✅ Token/FlashToken 已含 `username`/`nickname` claim（`AuthService.cpp` 已完成）
- ❌ `POST /api/oauth2/login/start` 不存在
- ❌ `GET /api/oauth2/login/check` 不存在
- ❌ `GET /login` 登录页不存在
- ❌ 回调通知目标仍是硬编码 `ueloginreturn://`，不支持动态 `redirect_uri`

## 执行顺序

```
U1 设备登录会话服务（内存存储）
  → U2 login/start 接口
  → U3 login/check 接口
  → U4 登录页 + 登录完成回写
  → U5 回调通知参数化（loopback redirect_uri）
  → U6 文档/错误码/验证
```

## 计划表

| 编号 | 任务 | 涉及文件 | 关键点 | 验证 |
|---|---|---|---|---|
| U1 | 新建 `DeviceLoginSessionService`：会话存储/过期/一次性消费 | `services/DeviceLoginSessionService.h/.cpp` | 内存 `unordered_map` + 时间戳 + `mutex/shared_mutex`；`state` 随机一次性；TTL 约 5–10 分钟；字段：`state, userId(-1 未登录), redirectUri, status, createdAt` | 单测/启动日志；并发读写安全 |
| U2 | 新增 `POST /api/oauth2/login/start` | `controllers/OAuth2Controller.h/.cc` | 接收 JSON body `{redirect_uri?}`；生成 `state`；返回 `{state, login_url, expires}`；`login_url = /login?state=...&redirect_uri=...` | curl 调用返回三字段 |
| U3 | 新增 `GET /api/oauth2/login/check?state=` | 同上 | 未完成 → `{active:false}`；已完成 → `{active:true, token, flashToken, username}`；**取后即废**；state 不存在/过期 → 错误码 | 未登录轮询 active:false；登录后 active:true；二次调用失败 |
| U4 | 新增登录页 `GET /login?state=&redirect_uri=` | `controllers/OAuth2Controller` + 新增 `views/oauth2_login.csp` | HTML 表单：账号密码登录 + 邮箱/手机验证码登录 + 第三方登录入口；登录成功后写 `state → userId`，再跳转通知 | 浏览器打开登录页可登录 |
| U5 | 登录成功后的回调通知参数化 | `services/ThirdPartyLoginService.cc` `CallbackRedirect` + `views/login_redirect.csp` | 若该 `state` 是 OAuth2 设备登录会话且带 `redirect_uri`，则跳 `redirect_uri`（仅通知，不传 token）；保留 `ueloginreturn://` 兼容 ueclient | 浏览器登录后跳 loopback；ueclient 流程不回退 |
| U6 | 补充文档与错误码 | `docs/API Reference Documentation.md`、`utils/ApiErrorCodes.h` | 新增接口文档；增加设备登录相关错误码（如 state 不存在/过期、已消费、redirect_uri 非法） | 文档与实现一致 |

## 接口契约（建议）

### `POST /api/oauth2/login/start`

请求：

```json
{ "redirect_uri": "http://127.0.0.1:45678/cb" }
```

响应：

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

> `redirect_uri` 只允许 loopback（`127.0.0.1` / `localhost`），否则拒绝；为空时保留现有 `ueloginreturn://` 兼容路径。

### `GET /api/oauth2/login/check?state=`

未完成：

```json
{ "code": 0, "msg": "success", "data": { "active": false } }
```

已完成（**一次性**）：

```json
{ "code": 0, "msg": "success", "data": {
  "active": true,
  "token": "...",
  "flashToken": "...",
  "username": "eve"
} }
```

### `GET /login?state=&redirect_uri=`

- 渲染登录页；
- 支持现有登录方式：
  - 密码登录 `/api/user/login/pwd`；
  - 邮箱/手机验证码登录；
  - 第三方登录（复用 `/api/third/...`）；
- 登录成功后把 `userId` 写入设备会话，然后按 `redirect_uri` 跳转通知。

## 实现要点 / 风险

1. **一次性消费**：`login/check` 取 token 后立即从会话表删除/标记 consumed，防重放。
2. **并发安全**：`DeviceLoginSessionService` 用 `shared_mutex` 或 Drogon `CacheMap`，避免并发 check 拿到两次。
3. **登录页安全**：
   - `state` 必须存在且未过期才允许进入登录页；
   - 登录成功后仅把 `userId` 写入 state，**不把 token 放进 redirect_uri**；
   - `redirect_uri` 白名单只放行 loopback 地址。
4. **兼容旧客户端**：`redirect_uri` 为空时仍使用 `ueloginreturn://`，ueclient 不受影响。
5. **登录方式复用**：优先直接调用现有 `AuthService::LoginByPwd` / `LoginByOther` / `ThirdPartyLoginService`，不要复制认证逻辑。
6. **有效期**：建议 state 5 分钟、登录页会话 10 分钟；具体可在 `config.yaml` 配置。

## 里程碑

| 里程碑 | 内容 | 验证 |
|---|---|---|
| U-M1 | 会话服务 + `login/start` | curl 创建会话，返回 state/login_url |
| U-M2 | `login/check` 轮询 | 未登录 active:false；登录后 active:true |
| U-M3 | 登录页 + 登录回写 | 浏览器密码登录成功，check 能取 token |
| U-M4 | 回调通知 loopback | 登录后浏览器跳 127.0.0.1；ueclient 保留 ueloginreturn |
| U-M5 | 端到端 | Pidgin 设备登录全流程 + 多客户端并发 |

## 建议

- **第一阶段先做 U1–U3 + 登录页最小可用（密码登录）**，即可供 Pidgin M5 联调；
- 第三方登录入口、验证码登录可作为第二阶段补齐；
- 服务端实现与 Pidgin 侧计划（`D:\vc\pidgin\doc\UEADMIN-OAUTH2-PIDGIN-PLAN.md`）并行推进，M5 联调依赖 U-M3 完成。

---

## 第一阶段实施记录（已完成）

### 已实现文件

| 文件 | 说明 |
|---|---|
| `services/DeviceLoginSessionService.h/.cpp` | 设备登录会话服务，基于 Drogon `CacheMap`，TTL 默认 600s，支持创建/查询/标记登录/提取/恢复 |
| `controllers/OAuth2Controller.h/.cc` | 新增 `login/start`、`login/check`、`GET /login`、`POST /login` |
| `views/oauth2_login.csp` | 密码登录页（第一阶段最小可用） |
| `config.yaml` | 新增 `UserManage.DeviceLoginSec: 600` |
| `main.cc` | 初始化 `DeviceLoginSessionService` |

### 已验证

- ✅ 构建成功（MSVC + vcpkg，需要 vcvars64 环境）
- ✅ `POST /api/oauth2/login/start` 返回 `state`、`login_url`、`expires`
- ✅ `GET /api/oauth2/login/check` 未登录返回 `active:false`，且不消费 state（可重复轮询）
- ✅ 非法 `redirect_uri`（非 loopback）返回 400
- ✅ `GET /login` 正常渲染登录页
- ✅ `POST /login` 错误密码返回 401，且 state 仍保持未登录
- ⏳ 真实账号登录成功 → `login/check` 返回 token 的端到端验证（需要测试账号）

### 端到端验证记录（2026-08-19）

- 使用账号 `User_95493544` / 密码 `Porthack123` 验证：
  - `POST /login` 返回 **401**；
  - `GET /api/oauth2/login/check` 保持 `active:false`；
  - 用现有 `/api/user/login/pwd` 直接登录同样返回 **-301 用户名或密码错误**。
- **结论**：该账号/密码在当前本地数据库中不可用，不是新接口逻辑问题；需要用有效账号完成最终端到端。

### 待办 / 后续阶段

- 用真实账号完成 U-M3 端到端验证
- U5：`ThirdPartyLoginService::CallbackRedirect` 支持动态 loopback `redirect_uri`（当前 `POST /login` 已实现 loopback 跳转，但第三方登录回调尚未参数化）
- U6：更新 API 文档与错误码
- 第三方登录、邮箱/手机验证码登录入口（第二阶段）

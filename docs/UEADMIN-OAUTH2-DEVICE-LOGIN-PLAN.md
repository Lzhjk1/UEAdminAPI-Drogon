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
- ✅ 真实账号登录成功 → `login/check` 返回 token 的端到端验证通过

### 端到端验证记录（2026-08-19）

- 使用账号 `roasal` / 密码 `Porthack123` 验证：
  - `POST /api/oauth2/login/start` → 返回 `state`、`login_url`、`expires`
  - `POST /login`（账号密码登录）→ 返回 **302** 到 `http://127.0.0.1:45678/cb`
  - `GET /api/oauth2/login/check?state=` → **`active:true`**，返回 `token`、`flashToken`、`username=roasal`
  - 第二次 `check` 同一 state → **已消费**，返回错误
- **结论**：CacheMap 覆盖修复后，设备登录全流程本地验证通过。

### 修复记录（CacheMap 不覆盖问题）

- **现象**：`POST /login` 返回 302，但 `login/check` 仍 `active:false`。
- **根因**：Drogon `CacheMap::insert()` 使用 `std::map::insert`，key 已存在时**不覆盖旧值**；`MarkLoggedIn` 无法把 `userId` 写回已存在的 state。
- **修复**：`DeviceLoginSessionService::MarkLoggedIn()` / `RestoreSession()` 更新前先 `erase(state)` 再 `insert(state, info, ttl)`。
- **影响文件**：`services/DeviceLoginSessionService.cpp`

### U5/U6 实施记录（已完成）

- ✅ U5：第三方登录回调通知参数化
  - `ThirdPartyLoginService::GetLoginUrl(platform, deviceState)`：支持从设备登录页携带 `ueadmin_state` 发起第三方登录，`ueadmin_state` 作为独立 query 参数追加到第三方 `authorizationUrl`。
  - `ThirdPartyLoginService::CallbackRedirect`：回调时剥离 `&ueadmin_state=`，还原原始第三方 state 后交给平台回调；若设备会话存在 loopback `redirect_uri`，渲染 `login_redirect.csp` 跳转到该地址（仅通知，不传 token）。同时若该第三方账号已绑定本地用户，会把 `userId` 写回设备会话，使 `/api/oauth2/login/check` 能返回 token/flashToken；未绑定场景保持 `userId=-1`，由客户端继续走绑定/注册。
  - `ThirdPartyLoginService::Callback`：同样剥离 `ueadmin_state`，保持旧 API 语义兼容。
  - `controllers/ThirdPartyLogin`：`GET /api/third/authorization_url?platform={1}&ueadmin_state={2}`。
  - 新增 `filters/DeviceLoginOptional.{h,cc}`：带 `ueadmin_state` 时校验设备会话存在；不带时放行（兼容旧客户端）。
- ✅ U6：文档与错误码
  - `utils/ApiErrorCodes.h` 新增：
    - `ApiError_DeviceLoginStateInvalid` = -701
    - `ApiError_DeviceLoginStateConsumed` = -702
    - `ApiError_DeviceLoginRedirectUriNotAllowed` = -703
  - `docs/API Reference Documentation.md` 新增 7.4–7.7 设备登录接口文档，并补充 4.1/4.2 第三方登录的 `ueadmin_state` 说明。
  - `docs/API_Error_Codes.md` 新增第 7 节“OAuth2 设备登录模块”错误码表。
- 遗留：线上部署、登录页邮箱/手机/第三方入口（第二阶段）。

### 待办 / 后续阶段

- 将修复后的版本部署到 `im.uesoft.com`（线上当前仍是旧版，`login/check` 会 active:false）
- 登录页增加邮箱/手机验证码登录入口（第二阶段）
- 第三方登录页入口 UI（当前已支持通过 `ueadmin_state` 参数对接，但 `views/oauth2_login.csp` 尚未渲染第三方登录按钮）

---

## 扫码登录与完成页实施记录（2026-09-17）

### 修复：`ueadmin_state` 透传（U5 遗留的真实缺陷）

U5 记录里"把 `ueadmin_state` 作为独立 query 参数追加到第三方 `authorizationUrl`"的做法**在真实回调中必然失效**：
QQ / 微信授权完成后只重定向到 `redirect_uri?code=..&state=..`（`redirect_uri` 本身是固定值），
任何附加的自定义 query 参数都会被平台丢弃。于是 `ParseUeAdminState(state)` 解析出的 `deviceState` 恒为空，
`CallbackRedirect` 里 `if (!deviceState.empty())` 那一整块永不进入、`MarkLoggedIn` 从不执行，
客户端 `/api/oauth2/login/check` 永远是 `active:false` —— 这条"登录页里走第三方登录"的路径**从未真正可用过**。

修法是不再依赖任何回传，改为服务端自己记住：

- `ThirdPartyLoginValue` 新增 `deviceState` 字段，`GetLoginUrl` 创建会话时写入；
- `CallbackRedirect` 按回调的 `state`（即第三方 code）反查该字段，取不到再回退 `ParseUeAdminState`（兼容旧格式）；
- `GetLoginUrl` 删掉了往 `authorizationUrl` 追加 `ueadmin_state` 的代码，响应新增 `appId` / `redirectUri`
  （公开值，供页内二维码实例化 `wxLogin.js` 用；`clientSecret` 不暴露）。

### 新增

| 内容 | 位置 |
|---|---|
| 登录页「扫码登录」tab：微信页内二维码（官方 `wxLogin.js`，`self_redirect=false`）+ QQ 跳转按钮 | `views/oauth2_login.csp` |
| 登录完成页 ✔「登录成功 / 可以关闭此窗口」 | `views/login_done.csp` + `GET /login/done` |
| 第三方账号未绑定 / 回调未完成时的提示页（**不跳 loopback**） | `views/third_party_unbound.csp` + `CallbackRedirect` 内判定 |
| 那一针的回归测试 + PDMS 不受影响的回归测试 + 完成页可访问 | `tests/test_third_party_device_login.py` |

关于完成页：服务端出页、客户端 loopback 收到通知后 302 过来，而不是让浏览器停在 loopback 上显示客户端返回的
纯文本 `OK`。之所以不复用 `login_redirect.csp`，是因为那一页会 `meta refresh` + `location.href` 跳
`customProtocol`，桌面客户端场景没有可跳目标，照搬会在原地打转。

### 仍未做

- **未绑定第三方账号的快速失败**：目前是"不跳 loopback + 渲染提示页"，客户端要等自己的 120s 超时。
  要做到秒级报错，需要在设备会话里存错误态并由 `login/check` 返回 —— 但客户端只在收到 loopback 通知后才轮询，
  所以还得同时把用户导到提示页（即客户端 loopback 需接受服务端给出的跳转路径）。本轮未做，先靠提示页文案兜住。
- 邮箱/手机验证码登录入口。
- 线上部署（含微信「网站应用」的授权回调域需包含 `im.uesoft.com`）。

### 踩到的坑（构建，非代码）

- **本项目的 Ninja 规则不跟踪头文件依赖**：只改 `.h` 时 `cmake --build` 会认为无事可做，
  编译 0 个文件、链接也不执行，得到一个**静默的旧二进制**。本次调试中它伪装成"新加的
  路由不生效"，白排查了一轮（一度误判成 drogon 的路由限制）。
  改头文件后请用 `cmake --build outuildd-Debug --clean-first`（或先删掉对应的 `.obj`）。
- 新增 `views/*.csp` 需要重新 configure（`drogon_create_views` 在 configure 期扫描目录）。

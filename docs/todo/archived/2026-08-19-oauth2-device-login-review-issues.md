# OAuth2 设备登录新增接口审核问题清单

> 审核范围：最近 3 次提交（`da64635..HEAD`）
> 提交：
> - `527c64f` feat(oauth2): 新增设备登录会话 login/start login/check 登录页 (M3)
> - `73717e6` feat(oauth2): 设备登录集成第三方 ueadmin_state 透传与错误码细化
> - `ff03b7c` test(oauth2): 补充设备登录与第三方登录透传 pytest 测试
>
> 记录日期：2026-08-19

---

## 🔴 高：`redirect_uri` 校验是“前缀匹配”，可被绕过

**位置**：`controllers/OAuth2Controller.cc` — `isAllowedLoopbackRedirectUri()`

**现状**：

```cpp
if (uri.rfind("http://127.0.0.1", 0) == 0 || uri.rfind("http://localhost", 0) == 0) {
    return true;
}
```

**问题**：
- `http://127.0.0.1.evil.com` ✅ 通过
- `http://localhost.evil.com` ✅ 通过
- `http://127.0.0.1@evil.com` ✅ 通过

虽然当前只用于“通知跳转”（不传 token），但可被用于钓鱼/诱导跳转。

**建议**：
- 解析 host 后严格校验 host 为 `127.0.0.1` / `localhost`（可再支持 IPv6 `[::1]`）。
- 只允许 http，不允许 https 之外的自定义 scheme（当前已限定 http 前缀，但应显式约束）。

---

## 🔴 高：第三方 `ueadmin_state` 直接拼入 URL，未做 URL 编码

**位置**：`services/ThirdPartyLoginService.cc` — `GetLoginUrl()`

**现状**：

```cpp
authUrl += (authUrl.find('?') == std::string::npos ? "?" : "&") + std::string("ueadmin_state=") + deviceState;
```

**问题**：
- `ueadmin_state` 虽然当前来自服务端 UUID，但 `GetLoginUrl` 是公开接口，未限制字符集。
- 若 state 格式变化或传入特殊字符，会破坏第三方 OAuth URL 或造成参数注入。

**建议**：
- 对 `deviceState` 做 URL encode。

---

## 🟠 中：`login/check` 的“未完成轮询”存在并发丢失风险

**位置**：`controllers/OAuth2Controller.cc` — `loginCheck()`

**现状**：
1. `ExtractSession(state)`：取出并删除
2. 若 `userId <= 0`：`RestoreSession(session)` 放回

**问题**：
- 两个并发 `check` 同时到达时，A 取出、B 失败；A 放回后，B 可能已经返回 `-701`。
- 不符合“未完成可继续轮询”的契约。

**建议**：
- 未完成时用 `FindSession`（不消费）；仅当 `userId > 0` 时才原子消费。
- 例如：`FindSession` → 未登录返回 active:false；已登录 → `ConsumeSession` 成功后返回 token。

---

## 🟠 中：`POST /login` 未校验 `redirect_uri` 与 session 中的 `redirect_uri` 一致

**位置**：`controllers/OAuth2Controller.cc` — `loginByPwd()`

**现状**：
- `loginStart` 创建 session 时已保存 `redirectUri`。
- `POST /login` 从请求体读取 `redirect_uri`，仅 `FindSession(state)`，未与 session 保存值比对，也未再次执行 loopback 白名单校验。

**问题**：
- 攻击者可构造 `POST /login { state: 合法state, redirect_uri: "https://evil.com" }`，登录成功后 302 到恶意地址。

**建议**：
- `POST /login` 的 `redirect_uri` 应取 session 中已保存的值，或至少与 session 值比对；
- 或者再次调用 `isAllowedLoopbackRedirectUri` 校验。

---

## 🟠 中：`POST /login` 对已消费/已登录 session 的错误语义不清晰

**位置**：`controllers/OAuth2Controller.cc` — `loginByPwd()`

**现状**：
- `MarkLoggedIn` 返回 false 时，接口返回 `-702 state 已登录或不存在`。

**问题**：
- 用户密码已验证成功，但 state 已被消费/已登录，返回“state 已登录或不存在”把“不存在”和“已消费”混在一起，语义不明确。

**建议**：
- 区分“不存在/过期”（`-701`）与“已消费/已登录”（`-702`）。
- 对已登录的 state，提示“该登录会话已完成，请直接使用 login/check 获取 token”。

---

## 🟡 低：`login/check` 查询用户失败时静默返回空 username

**位置**：`controllers/OAuth2Controller.cc` — `loginCheck()`

**现状**：

```cpp
try {
    auto user = mapper.findByPrimaryKey(session.userId);
    username = user.getValueOfName();
} catch (const std::exception &e) {
    LOG_ERROR << "loginCheck: 查询用户失败: " << e.what();
}
```

**问题**：
- 查询用户失败时仍返回 `active:true` + token + `username=""`。
- 如果用户刚被删除，客户端会拿到空用户名成功响应。

**建议**：
- 查询失败时返回明确错误（如 `-302` 用户不存在 / `-103` 内部错误），或至少返回明确错误信息。

---

## 🟡 低：设备登录页 CSP 模板没有 HTML 转义

**位置**：`views/oauth2_login.csp`

**现状**：

```html
<input type="hidden" name="state" value="[[state]]" />
<input type="hidden" name="redirect_uri" value="[[redirect_uri]]" />
```

**问题**：
- `state` 是 UUID，风险低；
- `redirect_uri` 来自 `GET /login` 查询参数，`loginPage` 直接透传到视图。
- Drogon CSP 的 `[[...]]` 默认不做 HTML escape，`GET /login?state=合法&redirect_uri="><script>` 存在 XSS 风险。

**建议**：
- 在视图层使用 `htmlTranslate`，或在 controller 对 `redirect_uri` 做 HTML 编码/严格校验。

---

## 🟡 低：`ueadmin_state` 回调解析依赖固定分隔符 `&ueadmin_state=`

**位置**：`services/ThirdPartyLoginService.cc` — `CallbackRedirect()` / `Callback()`

**现状**：

```cpp
auto pos = thirdPartyState.find("&ueadmin_state=");
deviceState = thirdPartyState.substr(pos + strlen("&ueadmin_state="));
```

**问题**：
- 如果第三方平台对 `state` 做了 URL encode，`&` 可能变成 `%26`，解析不到；
- 如果 `ueadmin_state` 后还有额外参数，`deviceState` 会混入多余内容。
- 当前假设平台原样回传且 `ueadmin_state` 在最后，测试未覆盖真实回调。

**建议**：
- 对回调中的 `state` 先 `urlDecode`；
- 用 `&` 分割后取 `ueadmin_state` 参数值。

---

## 🟡 低：文档与错误码小问题

**位置**：`docs/API Reference Documentation.md`、`docs/API_Error_Codes.md`

**问题**：
- `docs/API_Error_Codes.md` 把 `/login` GET 成功码写成 `0 / ApiError_Success`，但该接口实际返回 HTML，不是 JSON `{code:0}`。
- `docs/API Reference Documentation.md` 7.5 写错误码 `-308 更新用户状态失败`，与枚举 `-308 更新失败` 文案不一致。
- 测试 `test_login_start_success` 硬编码 `expires == 600`，配置文件修改 TTL 后测试会挂。

**建议**：
- 统一文档与实际返回格式；
- 测试改为读取配置或断言 `expires > 0`。

---

## 🟡 低：代码质量问题

**位置**：`controllers/OAuth2Controller.cc`、`services/DeviceLoginSessionService.cpp`

**问题**：
- `urlEncode` 自己实现，未复用 Drogon 的 `urlEncode`（重复造轮子）。
- `DeviceLoginSessionService::ConsumeSession` 目前无调用方（`loginCheck` 用的是 `ExtractSession`），存在死代码。
- `ThirdPartyLoginService::Callback`（非 Redirect 版）剥离 `ueadmin_state` 后调用平台回调，需确认是否有其他调用路径受影响。

**建议**：
- 使用 Drogon `urlEncode`；
- 删除或启用 `ConsumeSession`；
- 确认 `Callback` 的剥离逻辑不影响旧调用方。

---

## 修复优先级

1. **必须修**
   - `redirect_uri` 前缀校验改严格 loopback 解析
   - `POST /login` 校验 redirect_uri 与 session 一致或重新白名单校验

2. **建议修**
   - `login/check` 未完成轮询改为“先查后消费”避免并发误判
   - 回调 `ueadmin_state` 做 urlDecode + 参数分割

3. **尽快修**
   - `views/oauth2_login.csp` 对 `redirect_uri` 做 HTML 转义

4. **可选**
   - 去掉死代码 `ConsumeSession`
   - 使用 Drogon `urlEncode`
   - 修正文档与错误码文案

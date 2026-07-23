# UEAdminAPI 自动化测试套件

基于 pytest 的端到端接口测试。配合后端 **测试模式** 实现验证码无人值守获取。

## 前置条件

1. **后端开启测试模式**：在 `config.json` 的 `custom_config` 中启用：
   ```json
   "TestMode": {
       "Enable": true,
       "FixedCode": "",
       "ColdDownSec": 0
   }
   ```
   - `Enable: true` 时验证码不会真实发送(不调 SMTP / 短信网关)，而是直接入库。
   - `FixedCode` 指定固定验证码，**留空则随机生成**（默认）。测试侧统一通过调试接口 `GET /api/test/mfa/latest` 动态取回，无需与后端约定固定值。
   - `ColdDownSec: 0` 关闭发送冷却，便于快速用例。
   - **生产环境务必保持 `Enable: false`**。

2. **重新编译并启动后端**：修改 config 后需重启服务才能加载新配置（TestModeConfig 在服务启动时初始化）。

3. **安装 Python 依赖**：
   ```bash
   pip install -r tests/requirements.txt
   ```

## 运行

在项目根目录执行：

```bash
# 默认指向 http://127.0.0.1:80
pytest tests/

# 指定服务地址 / 邮箱域名
UEADMINAPI_BASE_URL=http://127.0.0.1:80 \
UEADMINAPI_TEST_DOMAIN=example.com \
pytest tests/
```

若服务不可达或测试模式未开启，套件会通过 `pytest.exit` 立即中止并提示原因。

## 调试查询接口

测试模式下后端新增一个调试接口供测试侧取回验证码：

- `GET /api/test/mfa/latest?target={target}&type={mfaType}`
  - 返回: `{ "code": 0, "data": { "code": "888888" } }`
  - 非测试模式启用时直接返回 `403`。

接口注册在 [TestMfaController.h](../controllers/TestMfaController.h) / [.cc](../controllers/TestMfaController.cc)。

## 目录结构

```
tests/
  conftest.py          # 全局夹具: testmode 闸门、账号注册/登录/回收
  api_client.py        # HTTP 客户端封装
  requirements.txt     # Python 依赖
  pytest.ini           # pytest 配置
  test_system.py       # /api/system/ping
  test_mfa.py          # 发送/校验验证码、调试接口取码
  test_actiontoken.py  # 匿名/登录后 ActionToken 获取与校验
  test_register.py     # 通用注册、快速注册、检查存在
  test_auth_login.py   # 密码/邮箱/手机/FlashToken 登录、token 校证、注销
  test_user_manage.py  # 修改昵称/性别/邮箱、解绑邮箱、删除用户
```

## 说明

- 每个用例产生的账号会在 teardown 中尝试删除以减少 DB 残留；teardown 失败时打印 `[WARN]` 警告但不阻断测试断言。`_delete_account` 在账号 token 已失效(如已注销)时会重新 `login_pwd` 后再删除。
- 有两套等价的注册夹具：
  - `fresh_email_account`：用独立 function 级 `fresh_client` 注册（推荐，状态隔离最干净）。
  - `registered_email_account`：用 session 级共享 `client` 注册（注册阶段共用 client，登录阶段另起 client）；多用于只读断言。
- 二者均封装"注册-登录-回收"完整生命周期，用例可直接使用夹具返回的 `TestAccount`（含 `username/password/email/client`）。
- 第三方登录(QQ / WeChat)因需要真实 OAuth 回调与第三方平台真实授权，未纳入自动化测试范围。需手工通过以下流程测试：
  1. `GET /api/third/authorization_url?platform=qq` 获取授权页地址
  2. 在浏览器中打开授权页并完成 QQ/微信登录授权
  3. 服务端回调 `/api/third/{platform}`，关联临时会话
  4. `GET /api/third/login/check` / `POST /api/third/bind` / `POST /api/third/register` 验证绑定与登录
  - TODO: 若后续需要在测试模式下端到端跑通，需在 `ThirdPartyLoginService` 引入 mock 支持(伪造 authorization_url + 可控 callback)。
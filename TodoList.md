# 记录
- ✅ 我也许是又违反了单一职责的道理, 这个bindAccount太繁琐了 (改成了需要权限的接口, 传入Token Header作身份验证, 不再现场使用账号密码来登录)
- TODO: 邮箱未注册时理应只能发送Register类型验证码 (这样会在接口引入数据库操作, 这样算下来应该不划算)
- ✅ drogon的content-type有单独的方法来设置, 使用通用的SetHeader没用, `req->setContentTypeCode(ContentType::CT_APPLICATION_JSON);`
- ✅ 先测试一下普通登录, 也就是账号密码, 配合手机或邮箱
- ✅ Gitlab的删除用户服务直接返回了network faliure
- ✅ 手机验证码通道因为有区号的设定, 需要打个补丁, 不然验证时用的号码没有区号就对不上了, 验证失败
- ✅ 获取验证码应有CD
- ✅ 将控制器接口中的部分代码, 比如登录和注册的流程, 封装为服务里的方法, 方便其它服务也可以调用, 毕竟接口是没办法方便调用的
- ✅ 封装的登录注册放在AuthService里
- 准备写第三方登陆callback之后, 根据第三方账号创建用户的接口
- ✅ Token的Status可能需要两个, 因为除了Token还有FlashToken Token的status已加但还没使用
- ✅ CheckToken函数没有区分Token和FlashToken, 也没有检查status, 以后要区分一下 已在AuthService修改CheckTokenAndParseUserId函数并添加函数CheckTokenStatus
- ✅ 验证第三方登陆是否成功的接口必须要独立, 不然一个没绑定过的第三方账号就会直接注册一个账号, 这样会失去绑定已有账号的机会
- 跟loginvalue相关的getAccessToken这样的get, set函数风格我觉得不好, getset应该属于loginvalue, 而不是platform, 也就是调用方式应为loginvalue->getAccessToken()这样,
- ✅ 清理头文件中的 std 命名空间引用, 这会导致极大的重名风险且不易察觉
- ✅ Login控制器的实现都移到了AuthService里, 方便重用, 已经测试无问题
- 可以尝试写一个await future类型的适配器, 用于将orm的那些数据库操作转换为异步操作
- ✅ 已经写好附带第三方平台登录的注册接口, 但是还没有测试
- 需要有刷新Gitlab Impersonation Token的机制
- 🟦需要有刷新第三方平台Access Token的机制 (其实暂时没必要, 因为每次登录都要经过的第三方登陆步骤都会重新获取AccessToken) 
- [💥比较严重]目前数据库里有一个表记录了第三方平台的id和对应的名字, 但是在代码里, 我是直接用一个枚举类型来表示平台的, 之后有机会可以改掉
- "Register控制器新增的可直接通过完成了第三方登陆的code对来注册账号" 这个功能可能不实用, 因为要发送多一次请求, 也许应该整合在第三方登陆那一边?
- ✅新增一个刷新Token的接口, 也可以说是通过FlashToken登录的接口, 还未测试
- ✅邱在PDMS的uesdk的UserManager::thirdLoginCheck需要修改
- pdms处Api::User::loginQQConfirm和微信的函数需要修改
- ✅缺少第三方解绑接口, /user/third/unbind
- 缺少更新用户信息接口, /user/update, , 需要header中的Token
- ✅缺少获取当前登录用户信息接口, /user/get/self, 需要header中的Token
- ✅缺少删除用户接口, /user/delete, 参数为1.邮箱或手机 2.验证码, 需要header中的Token
- ✅验证第三方登录接口增加参数onlyCheck, 用于指示是否只是确认登录, 而不是直接登录, 若为false, 则会尝试登录, 否则提示未绑定
- 目前腾讯手机验证码服务的模板不够多, 所以多出来的验证码类型, 都使用Default模板
- ✅之前应该有接口, 在指定邮箱或电话以及验证码后, 并没有验证这个邮箱或电话是否是用户绑定的, 待修正
- 注释了Token更新时的数据库操作
- status用更快的方式存储比较好, 因为status验证可能会很频繁, 不如直接在内存, 或者用redis等缓存
- 修改PDMS哪边的接口
    - login
- ✅统一Token放在名为"Authorization"的Header中, 格式为"Bearer {Token}"
- ✅Token验证有问题, 正在修复测试
- ✅在PDMS的HttpUtils里新增改造后的函数, 不直接返回NULL而是包含错误码和错误信息
- 在数据库, ThirdPartyInfo表里的外键约束加上了On Delete Cascade, 之后更改一下在draw.io的设计, 然后检查一下其他表
- ✅第三方登陆直接注册账号时, 在其函数里直接操作数据库来新增用户, 应该是直接调用AuthService下的注册接口才对
- ✅规范了返回的json里的code, 在一个枚举里
- ✅增加一个接口, 用于手机号直接注册账号完成登录
- AuthService下, DeleteUserForce不完善, 在删除用户对应的Gitlab账号时, 没有完善的异常处理
- ✅改为通过HttpFilter来验证Token, 并在Filter中解析UserId, 并将其设置到Request中, 后续控制器可以直接从Request中获取UserId, 如果Token无效或过期, 则直接返回401响应
- ✅增加ActionTokenFilter, MFA验证后获得指定接口的ActionToken后, 凭次Token访问受限接口, 文档在 docs\ActionToken_Architecture.md
- 通过flashToken刷新Token的接口目前没有使用Drogon的Filter实现
- [潜在问题]可能问题: 删除用户时, 因为Gitlab无法立刻删除, 需要等待一段时间后才能删除完毕, 所以删除用户后如果立刻注册同名用户会导致错误

# Token相关
- 普通Token无状态, 有效期在config.yaml中配置 `custom_config:UserManage:tokenExpeirSec`
- FlashToken有状态, 有效期在config.yaml中配置 `custom_config:UserManage:tokenFlashTokenSec`
- ActionToken不是JWT, 而是字符串, 在内存中保存, 验证后即失效, 有效期在config.yaml中配置 `custom_config:UserManage:ActionTokenSec`

# 第三方登录流程
1. 获取第三方登录页面
2. 扫码完成登录
3. callback收到相关信息, 完成第三方信息收集
4. 调用接口, 
    - 根据第三方账号创建用户, 还未实现
    - 或者绑定现有用户, 即bindAccount接口

# 接口总览
- 验证码 [SendVerifyCode.h](controllers/SendVerifyCode.h)
    - 手机验证码
    - 邮箱验证码
- 注册 [Register.h](controllers/Register.h)
- 登录 [Login.h](controllers/Login.h)
    - 密码登录 
    - 手机验证码登录
    - 邮箱验证码登录
    - 第三方登陆 [ThirdPartyLogin.h](controllers/ThirdPartyLogin.h)
        - 获取登陆页面
        - 第三方回调
        - 绑定账号 🚧未完成
        - 确认登录 🚧未完成
        - 解绑
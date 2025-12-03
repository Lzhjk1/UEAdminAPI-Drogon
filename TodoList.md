# 记录
- 我也许是又违反了单一职责的道理, 这个bindAccount太繁琐了 ✅ (改成了需要权限的接口, 传入Token Header作身份验证, 不再现场使用账号密码来登录)
- TODO: 邮箱未注册时理应只能发送Register类型验证码 (这样会在接口引入数据库操作, 这样算下来应该不划算)
- drogon的content-type有单独的方法来设置, 使用通用的SetHeader没用, `req->setContentTypeCode(ContentType::CT_APPLICATION_JSON);` ✅
- 先测试一下普通登录, 也就是账号密码, 配合手机或邮箱 ✅
- Gitlab的删除用户服务直接返回了network faliure ✅
- 手机验证码通道因为有区号的设定, 需要打个补丁, 不然验证时用的号码没有区号就对不上了, 验证失败 ✅
- 获取验证码应有CD ✅
- 将控制器接口中的部分代码, 比如登录和注册的流程, 封装为服务里的方法, 方便其它服务也可以调用, 毕竟接口是没办法方便调用的  ✅
- 封装的登录注册放在AuthService里  ✅
- 准备写第三方登陆callback之后, 根据第三方账号创建用户的接口
- Token的Status可能需要两个, 因为除了Token还有FlashToken ✅ Token的status已加但还没使用
- CheckToken函数没有区分Token和FlashToken, 也没有检查status, 以后要区分一下 ✅ 已在AuthService修改CheckTokenAndParseUserId函数并添加函数CheckTokenStatus
- 新修改的接口还没有测试, 之后记得测试
- 验证第三方登陆是否成功的接口必须要独立, 不然一个没绑定过的第三方账号就会直接注册一个账号, 这样会失去绑定已有账号的机会
- 跟loginvalue相关的getAccessToken这样的get, set函数风格我觉得不好, getset应该属于loginvalue, 而不是platform, 也就是调用方式应为loginvalue->getAccessToken()这样,
- 清理头文件中的 std 命名空间引用, 这会导致极大的重名风险且不易察觉 ✅


# 统一定义
- 密码登录 ✅
- 验证码 ✅ 定义基本相同, 区别在我这个不需要指定平台, 因为目前的情况下还是可以自动判断平台的
- 邮箱登录 ✅
- 电话登录 ✅

- 第三方登陆页面获取 ✅ authorUrl有歧义, 改用全称authorizationUrl
- 回调 ✅
- 绑定 ✅
- 确认第三方登录 邱的实现里, 如果没有绑定, 会自动注册账号

# 第三方登录流程
1. 获取第三方登录页面
2. 扫码完成登录
3. callback收到相关信息, 完成第三方信息收集
4. 调用接口, 
    - 根据第三方账号创建用户, 还未实现
    - 或者绑定现有用户, 即bindAccount接口

# 目前测试进程:
- 邮箱验证码 
- 邮箱注册
- 手机验证码
- 手机注册


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



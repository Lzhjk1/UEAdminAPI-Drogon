import yaml, sys

with open(sys.argv[1]) as f:
    c = yaml.safe_load(f)

cc = c.setdefault("custom_config", {})

um = cc.setdefault("UserManage", {})
um.setdefault("jwt_secret", "test_dev_secret_20260617")
um.setdefault("jwt_issuer", "ueadmin")
um.setdefault("ActionTokenSec", 300)

em = cc.setdefault("Email", {})
em.setdefault("SmtpServer", "smtp.placeholder.dev")
em.setdefault("SmtpPort", 465)
em.setdefault("EmailSource", "dev@placeholder.dev")
em.setdefault("EmailPassword", "placeholder_pwd")
em.setdefault("ClaimedName", "UEAdmin-Dev")

sms = cc.setdefault("SMS", {})
tencent = sms.setdefault("Tencent", {})
tencent.setdefault("Region", "ap-guangzhou")
sec = tencent.setdefault("Secret", {})
sec.setdefault("Id", "placeholder_id")
sec.setdefault("Key", "placeholder_key")
app = tencent.setdefault("SmsSdkApp", {})
app.setdefault("Id", "placeholder_app")
tencent.setdefault("SignName", "UEAdminDev")
tids = tencent.setdefault("TemplateIds", {})
for t in ["Login", "Register", "ResetPassword", "Default"]:
    tids.setdefault(t, "0")

gl = cc.setdefault("GitLab", {})
gl.setdefault("AdminToken", "placeholder_gitlab_token")
gl.setdefault("ApiHost", "gitlab.placeholder.dev")

qq = cc.setdefault("QQ", {})
qq.setdefault("AppId", "placeholder_qq_appid")
qq.setdefault("AppKey", "placeholder_qq_appkey")
qq.setdefault("RedirectUri", "https://placeholder.dev/qq/callback")

wc = cc.setdefault("WeChat", {})
wc.setdefault("AppId", "placeholder_wechat_appid")
wc.setdefault("AppSecret", "placeholder_wechat_secret")
wc.setdefault("RedirectUri", "https://placeholder.dev/wechat/callback")

cc.setdefault("ThirdPartyPlatformInfo", {})

with open(sys.argv[1], "w") as f:
    yaml.dump(c, f, default_flow_style=False, allow_unicode=True, sort_keys=False)

print("Config patched OK")

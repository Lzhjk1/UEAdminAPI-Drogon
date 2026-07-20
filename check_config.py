import yaml
import sys

try:
    with open(sys.argv[1]) as f:
        c = yaml.safe_load(f)

    # 1. yaml syntax
    print("YAML_SYNTAX:OK")

    # 2. UserManage.jwt_secret
    um = c.get("custom_config", {}).get("UserManage", {})
    secret = um.get("jwt_secret", "")
    issuer = um.get("jwt_issuer", "")
    print(f"JWT_SECRET:{'OK' if secret and secret != 'change-me-to-a-strong-random-secret' else 'MISSING'}")
    print(f"JWT_ISSUER:{'OK' if issuer else 'MISSING'}")

    # 3. db_clients
    dbs = c.get("db_clients", [])
    if dbs:
        print(f"DB_HOST:{dbs[0].get('host','')}")
        print(f"DB_PORT:{dbs[0].get('port','')}")
        print(f"DB_NAME:{dbs[0].get('dbname','')}")
    else:
        print("DB_HOST:")

    # 4. optional services
    sms_id = c.get("custom_config", {}).get("SMS", {}).get("Tencent", {}).get("Secret", {}).get("Id", "")
    email = c.get("custom_config", {}).get("Email", {}).get("SmtpServer", "")
    gitlab = c.get("custom_config", {}).get("GitLab", {}).get("AdminToken", "")
    qq = c.get("custom_config", {}).get("QQ", {}).get("AppId", "")
    wechat = c.get("custom_config", {}).get("WeChat", {}).get("AppId", "")
    print(f"SMS_CONFIGURED:{'YES' if sms_id else 'NO'}")
    print(f"EMAIL_CONFIGURED:{'YES' if email else 'NO'}")
    print(f"GITLAB_CONFIGURED:{'YES' if gitlab else 'NO'}")
    print(f"QQ_CONFIGURED:{'YES' if qq else 'NO'}")
    print(f"WECHAT_CONFIGURED:{'YES' if wechat else 'NO'}")

except yaml.YAMLError as e:
    print(f"YAML_SYNTAX:FAIL:{e}")
except Exception as e:
    print(f"ERROR:{e}")

import requests
import json
import argparse
import sys
import os

BASE_URL = "https://im.uesoft.com"
SESSION_FILE = "session.json"

class APIClient:
    def __init__(self):
        self.session = self.load_session()
        self.headers = {}
        if "token" in self.session:
            self.headers["Authorization"] = f"Bearer {self.session['token']}"
        if "action_token" in self.session:
            self.headers["X-Action-Token"] = self.session["action_token"]

    def load_session(self):
        if os.path.exists(SESSION_FILE):
            try:
                with open(SESSION_FILE, "r") as f:
                    return json.load(f)
            except Exception:
                return {}
        return {}

    def save_session(self, updates):
        self.session.update(updates)
        with open(SESSION_FILE, "w") as f:
            json.dump(self.session, f, indent=4)
        if "token" in updates:
            self.headers["Authorization"] = f"Bearer {updates['token']}"
        if "action_token" in updates:
            self.headers["X-Action-Token"] = updates["action_token"]

    def _request(self, method, url, **kwargs):
        kwargs["headers"] = {**self.headers, **kwargs.get("headers", {})}
        print(f"--> {method} {url} kwargs: {kwargs}")
        try:
            resp = requests.request(method, url, **kwargs)
            print(f"<-- {resp.status_code}")
            try:
                res_json = resp.json()
                print(json.dumps(res_json, indent=2, ensure_ascii=False))
                return res_json
            except ValueError:
                print(resp.text)
                return None
        except Exception as e:
            print(f"Error: {e}")
            return None

    def ping(self):
        return self._request("GET", f"{BASE_URL}/system/ping")

    def send_mfa(self, target, type_):
        return self._request("GET", f"{BASE_URL}/user/mfa", params={"target": target, "type": type_})

    def get_action_token_anonymous(self, mfa_type, mfa_code, target):
        res = self._request("GET", f"{BASE_URL}/user/action_token/anonymous", params={
            "mfaType": mfa_type,
            "mfaCode": mfa_code,
            "target": target
        })
        if res and res.get("code") == 0:
            self.save_session({"action_token": res["data"]["actionToken"]})
        return res

    def get_action_token(self, mfa_type, mfa_code=None, target=None):
        params = {"mfaType": mfa_type}
        if mfa_code: params["mfaCode"] = mfa_code
        if target: params["target"] = target
        res = self._request("GET", f"{BASE_URL}/user/action_token", params=params)
        if res and res.get("code") == 0:
            self.save_session({"action_token": res["data"]["actionToken"]})
        return res

    def register_phone(self, phone):
        res = self._request("POST", f"{BASE_URL}/user/create/phone", params={"phone": phone})
        return res

    def login_phone(self, phone):
        res = self._request("GET", f"{BASE_URL}/user/login/phone", params={"phone": phone})
        if res and res.get("code") == 0:
            self.save_session({"token": res["data"]["token"], "flashToken": res["data"].get("flashToken")})
        return res

    def login_email(self, email):
        res = self._request("GET", f"{BASE_URL}/user/login/email", params={"email": email})
        if res and res.get("code") == 0:
            self.save_session({"token": res["data"]["token"], "flashToken": res["data"].get("flashToken")})
        return res

        
    def get_self(self):
        return self._request("GET", f"{BASE_URL}/user/self")

    def third_auth_url(self, platform):
        res = self._request("GET", f"{BASE_URL}/api/third/authorization_url", params={"platform": platform})
        if res and res.get("code") == 0:
            self.save_session({
                "third_code": res["data"]["code"],
                "third_verifyCode": res["data"]["verifyCode"]
            })
        return res

    def third_login_check(self, platform, code, verify_code, only_check=True):
        res = self._request("GET", f"{BASE_URL}/api/third/login/check", params={
            "platform": platform,
            "code": code,
            "verifyCode": verify_code,
            "onlyCheck": str(only_check).lower()
        })
        if res and res.get("code") == 0 and res.get("data", {}).get("token"):
            self.save_session({"token": res["data"]["token"]})
        return res

    def third_register(self, platform, code, verify_code):
        res = self._request("POST", f"{BASE_URL}/api/third/register", params={
            "platform": platform,
            "code": code,
            "verifyCode": verify_code
        })
        if res and res.get("code") == 0 and res.get("data", {}).get("token"):
            self.save_session({"token": res["data"]["token"]})
        return res

    def third_bind(self, platform, code, verify_code):
        res = self._request("POST", f"{BASE_URL}/api/third/bind", params={
            "platform": platform,
            "code": code,
            "verifyCode": verify_code
        })
        return res

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("command", help="Command to run")
    parser.add_argument("--target", help="Email or phone")
    parser.add_argument("--type", help="MFA Type (e.g. Register, Login, LoginOrRegister)")
    parser.add_argument("--code", help="MFA Code")
    parser.add_argument("--platform", help="Third party platform (qq, wechat)")
    parser.add_argument("--only-check", type=bool, default=True)
    args = parser.parse_args()

    client = APIClient()

    if args.command == "ping":
        client.ping()
    elif args.command == "mfa":
        client.send_mfa(args.target, args.type)
    elif args.command == "action_token_anon":
        client.get_action_token_anonymous(args.type, args.code, args.target)
    elif args.command == "action_token":
        client.get_action_token(args.type, args.code, args.target)
    elif args.command == "register_phone":
        client.register_phone(args.target)
    elif args.command == "login_phone":
        client.login_phone(args.target)
    elif args.command == "login_email":
        client.login_email(args.target)
    elif args.command == "self":
        client.get_self()
    elif args.command == "third_auth":
        client.third_auth_url(args.platform)
    elif args.command == "third_check":
        session = client.load_session()
        client.third_login_check(args.platform, session.get("third_code"), session.get("third_verifyCode"), args.only_check)
    elif args.command == "third_register":
        session = client.load_session()
        client.third_register(args.platform, session.get("third_code"), session.get("third_verifyCode"))
    elif args.command == "third_bind":
        session = client.load_session()
        client.third_bind(args.platform, session.get("third_code"), session.get("third_verifyCode"))
    else:
        print("Unknown command")

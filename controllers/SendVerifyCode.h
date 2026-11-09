#pragma once

#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>
#include "services/MFAService.h"
#include "utils/TestModeConfig.h"
#include <chrono>
#include <deque>
#include <unordered_map>

using namespace drogon;

class SendVerifyCode : public drogon::HttpController<SendVerifyCode>
{
public:
    METHOD_LIST_BEGIN
    // use METHOD_ADD to add your custom processing function here;
    // METHOD_ADD(SendVerifyCode::get, "/{2}/{1}", Get); // path is /SendVerifyCode/{arg2}/{arg1}
    // METHOD_ADD(SendVerifyCode::your_method_name, "/{1}/{2}/list", Get); // path is /SendVerifyCode/{arg1}/{arg2}/list
    // ADD_METHOD_TO(SendVerifyCode::your_method_name, "/absolute/path/{1}/{2}/list", Get); // path is /absolute/path/{arg1}/{arg2}/list
    ADD_METHOD_TO(SendVerifyCode::SendCode, "/api/user/mfa?target={1}&type={2}", Get); // path is /SendVerifyCode/{arg1}/{arg2}
    ADD_METHOD_TO(SendVerifyCode::CheckCode, "/api/user/mfa/check?target={1}&mfaCode={2}&type={3}", Get);
    // 验证码是认证凭证，新客户端走 POST body；GET 保留过渡。
    ADD_METHOD_TO(SendVerifyCode::SendCodePost, "/api/user/mfa", Post);
    ADD_METHOD_TO(SendVerifyCode::CheckCodePost, "/api/user/mfa/check", Post);

    METHOD_LIST_END
    // your declaration of processing function maybe like this:
    // void get(const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&callback, int p1, std::string p2);
    // void your_method_name(const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&callback, double p1, int p2) const;

    // 发送验证码, 支持邮箱和手机号, 自动识别类型, 有冷却时间, 
    // 因冷却时间导致的错误, 错误码为-2
    Task<HttpResponsePtr> SendCode(HttpRequestPtr req, std::string target, std::string type);
    Task<HttpResponsePtr> SendCodePost(HttpRequestPtr req);

    // 检查验证码是否正确，但不消耗该验证码
    Task<HttpResponsePtr> CheckCode(HttpRequestPtr req, std::string target, std::string mfaCode, std::string type);
    Task<HttpResponsePtr> CheckCodePost(HttpRequestPtr req);

    // 基于IP地址的冷却, 目前服务器是通过frp穿透来测试的, 所以IP是固定的, 所以冷却时间目前设置很短
    bool IsInColdDown(std::string ipAddr);

    // 获取当前生效的冷却时间(秒): 测试模式启用时取 TestMode.ColdDownSec, 否则取默认 2 秒
    int EffectiveColdDown() const;

    // 按目标限制发码频率，避免同一邮箱/手机被反复轰炸或用于穷举验证码。
    bool IsTargetRateLimited(const std::string &target);
    void RecordSuccessfulSend(const std::string &target);

private:
    std::mutex _mutexForColdDownMap;
    int _coldDownTime = 2; // 冷却时间(非测试模式默认)
    std::unordered_map<std::string, std::chrono::system_clock::time_point> _coldDownMap;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> _targetLastSent;
    std::unordered_map<std::string, std::deque<std::chrono::steady_clock::time_point>> _targetSendHistory;
};

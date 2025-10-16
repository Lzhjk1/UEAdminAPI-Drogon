//#include "MFA_Channels.h"
//#include "MFACodePair.h"
//
//// ========== MFAChannelBase ==========
//
//MFAChannelBase::MFAChannelBase(eChannelType channelType)
//    : _channelType(channelType) {}
//
//eChannelType MFAChannelBase::channelType() const {
//    return _channelType;
//}
//
//std::optional<std::shared_ptr<ICodePair>> MFAChannelBase::getCodePair(const std::string& key) {
//    std::lock_guard lock(_mutex);
//    for (const auto& p : _listCodePairs) {
//        if (p->baseInfo() == key)
//            return p;
//    }
//    return std::nullopt;
//}
//
//bool MFAChannelBase::verifyTheCode(const std::string& baseInfo, const std::string& code, eMFAType type, std::string& errorMsg) {
//    clearExpired();
//    auto target = getCodePair(baseInfo);
//    if (!target) {
//        errorMsg = "验证码不存在或已过期";
//        return false;
//    }
//    bool result = (target.value()->code() == code) && ((target.value()->mfaType() & type) != eMFAType::None);
//    if (result) {
//        std::lock_guard lock(_mutex);
//        _listCodePairs.erase(std::remove(_listCodePairs.begin(), _listCodePairs.end(), target.value()), _listCodePairs.end());
//    }
//    errorMsg = result ? "" : "验证码错误";
//    return result;
//}
//
//void MFAChannelBase::clearExpired() {
//    std::lock_guard lock(_mutex);
//    _listCodePairs.erase(
//        std::remove_if(_listCodePairs.begin(), _listCodePairs.end(),
//            [](const std::shared_ptr<ICodePair>& p) { return p->isExpired(); }),
//        _listCodePairs.end());
//}
//
//bool MFAChannelBase::addCodePairToList(std::shared_ptr<ICodePair> codePair) {
//    clearExpired();
//    if (!codePair->isEverythingAllSet())
//        return false;
//    std::lock_guard lock(_mutex);
//    auto it = std::find_if(_listCodePairs.begin(), _listCodePairs.end(),
//        [&](const std::shared_ptr<ICodePair>& p) {
//            return p->baseInfo() == codePair->baseInfo() && p->code() == codePair->code();
//        });
//    if (it != _listCodePairs.end())
//        _listCodePairs.erase(it);
//    _listCodePairs.push_back(codePair);
//    return true;
//}
//
//eChannelType MFAChannelBase::determineChannelType(const std::string& target) {
//    if (target.empty())
//        throw std::invalid_argument("target不能传入空值!");
//    static std::regex phoneRegex(R"(^1\d{10}$)");
//    static std::regex emailRegex(R"(^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$)");
//    if (std::regex_match(target, phoneRegex))
//        return eChannelType::SMS;
//    if (std::regex_match(target, emailRegex))
//        return eChannelType::Email;
//    return eChannelType::None;
//}
//
//// ========== MFA_EmailChannel ==========
//
//MFA_EmailChannel::MFA_EmailChannel(std::shared_ptr<drogon::Logger> logger, std::shared_ptr<EmailHelper> emailHelper)
//    : MFAChannelBase(eChannelType::Email), _logger(std::move(logger)), _emailHelper(std::move(emailHelper)) {}
//
//drogon::Task<std::pair<bool, std::string>> MFA_EmailChannel::sendCode(std::shared_ptr<ICodePair> codePair) {
//    if (!codePair) {
//        _logger->error("验证码发送失败, 给出的验证码为空");
//        co_return {false, "验证码发送失败, 给出的验证码为空"};
//    }
//    if (!codePair->isEverythingAllSet()) {
//        _logger->error("验证码发送失败, 给出的验证码信息不完整");
//        co_return {false, "验证码发送失败, 给出的验证码信息不完整"};
//    }
//    std::string subject = "优易软件验证码";
//    std::string content = "您的验证码为: " + codePair->code();
//    bool sent = co_await _emailHelper->sendEmail(codePair->baseInfo(), subject, content);
//    if (!sent) {
//        _logger->error("验证码发送失败, 邮件发送失败");
//        co_return {false, "验证码发送失败, 邮件发送失败"};
//    }
//    addCodePairToList(codePair);
//    co_return {true, "验证码发送成功"};
//}
//
//// ========== MFA_SMSChannel ==========
//
//MFA_SMSChannel::MFA_SMSChannel(std::shared_ptr<drogon::Logger> logger, std::shared_ptr<drogon::ConfigLoader> config)
//    : MFAChannelBase(eChannelType::SMS), _logger(std::move(logger)), _config(std::move(config)) {
//    // 这里假设你有自己的配置加载方式
//    _secretId = _config->get("SMS.Tencent.Secret.Id", "");
//    _secretKey = _config->get("SMS.Tencent.Secret.Key", "");
//    _region = _config->get("SMS.Tencent.Region", "");
//    _smsSdkAppId = _config->get("SMS.Tencent.SmsSdkApp.Id", "");
//    _signName = _config->get("SMS.Tencent.SignName", "");
//    // _templateIds 需要你自己实现从配置加载
//}
//
//drogon::Task<std::pair<bool, std::string>> MFA_SMSChannel::sendCode(std::shared_ptr<ICodePair> codePair) {
//    if (!codePair) {
//        _logger->error("验证码发送失败, 给出的验证码为空");
//        co_return {false, "验证码发送失败, 给出的验证码为空"};
//    }
//    if (!codePair->isEverythingAllSet()) {
//        _logger->error("验证码发送失败, 给出的验证码信息不完整");
//        co_return {false, "验证码发送失败, 给出的验证码信息不完整"};
//    }
//    // 这里以 HTTP POST 方式调用第三方短信服务为例
//    Json::Value reqJson;
//    reqJson["SmsSdkAppId"] = _smsSdkAppId;
//    reqJson["SignName"] = _signName;
//    reqJson["TemplateId"] = _templateIds[codePair->mfaType()];
//    reqJson["TemplateParamSet"] = Json::arrayValue;
//    reqJson["TemplateParamSet"].append(codePair->code());
//    reqJson["PhoneNumberSet"] = Json::arrayValue;
//    reqJson["PhoneNumberSet"].append(codePair->baseInfo());
//
//    auto client = drogon::HttpClient::newHttpClient("https://sms.tencentcloudapi.com");
//    auto req = drogon::HttpRequest::newHttpJsonRequest(reqJson);
//    req->setMethod(drogon::Post);
//
//    auto resp = co_await client->sendRequestCoro(req);
//    if (resp->getStatusCode() != drogon::k200OK) {
//        _logger->error("短信验证码发送失败, 号码: {}, 错误信息: {}", codePair->baseInfo(), resp->getBody());
//        co_return {false, "短信验证码发送失败, 号码: " + codePair->baseInfo()};
//    }
//    // 你可以根据 resp->getBody() 进一步解析返回内容
//    addCodePairToList(codePair);
//    co_return {true, "短信验证码发送成功"};
//}
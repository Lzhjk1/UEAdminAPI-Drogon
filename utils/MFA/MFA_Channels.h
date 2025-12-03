#pragma once
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <unordered_map>
#include <vector>
#include "ICodePair.h"
#include "IEmailService.h"
#include "MFACodePair.h"
#include "eMFA_Type.h"
#include "services/TencentSMSService.h"

enum class eChannelType {
    SMS,
    Email,
    None
};

// 验证码发送渠道接口
class IMFAChannel {
public:
    virtual ~IMFAChannel() = default;

    // 返回自身渠道类型枚举
    virtual eChannelType ChannelType() const = 0;

    // 根据key和类型获取验证码对
    virtual optional<shared_ptr<ICodePair>> GetCodePair(const std::string &key, const eMFAType& type) = 0;

    // 发送验证码
    virtual drogon::Task<pair<bool, std::string>> SendCode(shared_ptr<ICodePair> codePair) = 0;

    // 检查验证码是否正确, 获取到的验证码将被清除
    virtual bool VerifyTheCode(const std::string &baseInfo, const std::string &code, eMFAType type, std::string &errorMsg) = 0;
};

// 验证码发送渠道基类, 实现渠道接口, 并提供一些公共方法
class MFAChannelBase : public IMFAChannel {
protected:
    mutable mutex _mutex;
    vector<shared_ptr<ICodePair>> _listCodePairs;
    eChannelType _channelType;

    // 清除过期的验证码对
    void ClearExpired();

    // 添加验证码对到列表
    bool AddCodePairToList(shared_ptr<ICodePair> codePair);

public:
    // 仅显式调用的构造函数
    explicit MFAChannelBase(eChannelType channelType);

    // 返回自身渠道类型枚举
    eChannelType ChannelType() const override;

    // 根据key和类型获取验证码对
    optional<shared_ptr<ICodePair>> GetCodePair(const std::string &key, const eMFAType& type) override;

    // 检查验证码是否正确, 获取到的验证码将被清除
    bool VerifyTheCode(const std::string &baseInfo, const std::string &code, eMFAType type, std::string &errorMsg) override;

    // 静态方法, 根据目标字符串判断渠道类型, 返回类型枚举
    static eChannelType DetermineChannelType(const std::string &target);
};

// 邮件发送渠道
class MFA_EmailChannel : public MFAChannelBase {
    IEmailService *_emailService;
public:
    // 构造函数, 传入邮件服务接口实例
    explicit MFA_EmailChannel(IEmailService* emailService);
    // 发送验证码
    drogon::Task<pair<bool, std::string>> SendCode(shared_ptr<ICodePair> codePair) override;
};

// 短信发送渠道
class MFA_SMSChannel : public MFAChannelBase {
    ISmsService *_smsService;
public:
    // 构造函数, 传入短信服务接口实例
    explicit MFA_SMSChannel(ISmsService *smsService);
    // 发送验证码
    drogon::Task<pair<bool, std::string>> SendCode(shared_ptr<ICodePair> codePair) override;
    // 基类中的方法没有处理电话号的区号, 所以再重载一下
    bool VerifyTheCode(const std::string &baseInfo, const std::string &code, eMFAType type, std::string &errorMsg) override;
};
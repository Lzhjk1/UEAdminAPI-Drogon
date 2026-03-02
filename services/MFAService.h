#pragma once

#include <iostream>
#include <utils/MFA/eMFA_Type.h>
#include "utils/SingletonWithInit.h"
#include "utils/MFA/IEmailService.h"
#include "utils/MFA/ISmsService.h"
#include "utils/MFA/MFA_Channels.h"
#include <memory>
#include <mutex>
#include <vector>

class IMFAService {
public:
    virtual ~IMFAService() = default;
    virtual Task<std::tuple<bool, std::string>> SendTheCode(const std::string& target, eMFAType type) = 0;
    virtual Task<std::tuple<bool, std::string>> VerifyTheCode(const std::string& target, const std::string& code, eMFAType type, bool isConsume = true) = 0;
};

class MFAService : public IMFAService, public SingletonWithInit<MFAService> {
private:
    std::mutex _mutex;
    std::vector<std::shared_ptr<IMFAChannel>> _channels;

    std::shared_ptr<IMFAChannel> GetChannel(eChannelType type);

public:
    MFAService(IEmailService* emailService, ISmsService* smsService);
    Task<std::tuple<bool, std::string>> SendTheCode(const std::string& target, eMFAType type) override;
    
    /**
     * @brief 验证多因素认证代码
     * 
     * 此方法用于验证用户提供的MFA代码是否有效，支持多种类型的MFA验证。
     * 
     * @param target 目标标识符，通常是用户名、邮箱或手机号等唯一标识
     * @param code 用户提供的验证码
     * @param type 验证码用途枚举值，表示不同的验证场景（如修改密码、绑定手机等）
     * @param isConsume 验证成功后是否消耗验证码，默认为true
     * @return Task<std::tuple<bool, std::string>> 异步返回验证结果，包含状态码或错误信息, 
     * 
     * 第一个参数表示是否成功, 第二个表示错误信息
     */
    Task<std::tuple<bool, std::string>> VerifyTheCode(const std::string& target, const std::string& code, eMFAType type, bool isConsume = true) override;
};


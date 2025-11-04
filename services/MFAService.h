#pragma once

#include <iostream>
#include <utils/ServiceResponse.h>
#include <utils/MFA/eMFA_Type.h>
#include "utils/MFA/MFA_Manager.h"
#include "utils/SingletonWithInit.h"

class IMFAService {
public:
    virtual ~IMFAService() = default;
    virtual Task<ServiceResponse<int>> SendTheCode(const std::string& target, eMFAType type) = 0;
    virtual Task<ServiceResponse<int>> VerifyTheCode(const std::string& target, const std::string& code, eMFAType type) = 0;
};

class MFAService : public IMFAService, public SingletonWithInit<MFAService> {
private:
    MFA_Manager* _manager;
public:
    MFAService(IEmailService* emailService, ISmsService* smsService);
    Task<ServiceResponse<int>> SendTheCode(const std::string& target, eMFAType type) override;
    Task<ServiceResponse<int>> VerifyTheCode(const std::string& target, const std::string& code, eMFAType type) override;
};


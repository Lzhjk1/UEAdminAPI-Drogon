#pragma once

#include "MFAService.h"

MFAService::MFAService(IEmailService* emailService, ISmsService* smsService) {
    _manager = new MFA_Manager(emailService, smsService);
}

Task<ServiceResponse<int>> MFAService::SendTheCode(const std::string &target, eMFAType type) {
    auto res = co_await _manager->SendTheCode(target, "", type);
    co_return ServiceResponse<int>(0, res.first, res.second);
}

Task<ServiceResponse<int>> MFAService::VerifyTheCode(const std::string &target, const std::string &code, eMFAType type) {
    auto res = _manager->VerifyTheCode(target, code, type);
    co_return ServiceResponse<int>(0, res.first, res.second);
}

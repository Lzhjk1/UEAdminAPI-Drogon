#pragma once

#include "MFAService.h"

MFAService::MFAService(IEmailService* emailService, ISmsService* smsService) {
    _manager = new MFA_Manager(emailService, smsService);
}

Task<std::tuple<bool, std::string>> MFAService::SendTheCode(const std::string &target, eMFAType type) {
    co_return co_await _manager->SendTheCode(target, "", type);
}

Task<std::tuple<bool, std::string>> MFAService::VerifyTheCode(const std::string &target, const std::string &code, eMFAType type) {
    co_return _manager->VerifyTheCode(target, code, type);
}

#include "SendVerifyCode.h"

// Add definition of your processing function here

Task<HttpResponsePtr> SendVerifyCode::SendCode(HttpRequestPtr req, std::string target, int channel, int type, TencentSMSService* smsService) {
    co_return nullptr;
}

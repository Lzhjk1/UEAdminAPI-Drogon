#include "SendVerifyCode.h"
#include "utils/HttpResult.h"

// Add definition of your processing function here

using namespace UEAdminAPI;
using namespace UEAdminAPI::utils;

Task<HttpResponsePtr> SendVerifyCode::SendCode(HttpRequestPtr req, std::string target, std::string type) {
	auto resp = HttpResponse::newHttpResponse();
    auto mfaService = MFAService::Instance();
    auto [isSuccess, errMsg] = co_await mfaService->SendTheCode(target, stringToMFAType(type));

    HttpResult result;

    result.code = isSuccess ? 0 : -1;
    if(!isSuccess) {
        result.msg = errMsg;
    }
    
    resp->setBody(result.toJsonString());
    co_return resp;
}

#include "SendVerifyCode.h"

// Add definition of your processing function here

Task<HttpResponsePtr> SendVerifyCode::SendCode(HttpRequestPtr req, std::string target, std::string type) {
	auto resp = HttpResponse::newHttpResponse();
    auto mfaService = MFAService::Instance();
    auto res = co_await mfaService->SendTheCode(target, stringToMFAType(type));

    auto jsonBody = res.ToJson();

    resp->setBody(jsonBody.toStyledString());
    co_return resp;
}

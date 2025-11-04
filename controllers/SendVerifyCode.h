#pragma once

#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>
#include "services/MFAService.h"

using namespace drogon;

class SendVerifyCode : public drogon::HttpController<SendVerifyCode>
{
  public:
    METHOD_LIST_BEGIN
    // use METHOD_ADD to add your custom processing function here;
    // METHOD_ADD(SendVerifyCode::get, "/{2}/{1}", Get); // path is /SendVerifyCode/{arg2}/{arg1}
    // METHOD_ADD(SendVerifyCode::your_method_name, "/{1}/{2}/list", Get); // path is /SendVerifyCode/{arg1}/{arg2}/list
    // ADD_METHOD_TO(SendVerifyCode::your_method_name, "/absolute/path/{1}/{2}/list", Get); // path is /absolute/path/{arg1}/{arg2}/list
    ADD_METHOD_TO(SendVerifyCode::SendCode, "/SendVerifyCode?target={1}&type={2}", Post); // path is /SendVerifyCode/{arg1}/{arg2}

    METHOD_LIST_END
    // your declaration of processing function maybe like this:
    // void get(const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&callback, int p1, std::string p2);
    // void your_method_name(const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&callback, double p1, int p2) const;

    Task<HttpResponsePtr> SendCode(HttpRequestPtr req, std::string target, std::string type);
};

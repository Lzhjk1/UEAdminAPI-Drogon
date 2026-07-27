#pragma once

#include <drogon/HttpSimpleController.h>
#include <drogon/HttpController.h>

using namespace drogon;

class TestController : public drogon::HttpController<TestController>
{
public:
	METHOD_LIST_BEGIN
	// use METHOD_ADD to add your custom processing function here;
	ADD_METHOD_TO(TestController::TestHandler, "/api/test", Get); // path is /demo/v1/User/{arg2}/{arg1}
	// METHOD_ADD(User::your_method_name, "/{1}/{2}/list", Get); // path is /demo/v1/User/{arg1}/{arg2}/list
	// ADD_METHOD_TO(User::your_method_name, "/absolute/path/{1}/{2}/list", Get); // path is /absolute/path/{arg1}/{arg2}/list
	METHOD_LIST_END

    Task<HttpResponsePtr> TestHandler(HttpRequestPtr req);

};

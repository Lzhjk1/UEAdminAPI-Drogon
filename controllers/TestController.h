#pragma once

#include <drogon/HttpSimpleController.h>
#include <drogon/HttpController.h>

using namespace drogon;

class TestController : public drogon::HttpController<TestController>
{
public:
	METHOD_LIST_BEGIN
	// 调试接口曾会无鉴权创建 GitLab Impersonation Token；保留给后台联调，但必须超管鉴权。
	ADD_METHOD_TO(TestController::TestHandler, "/api/test", Get, "AuthFilter", "SuperAdminFilter");
	METHOD_LIST_END

    Task<HttpResponsePtr> TestHandler(HttpRequestPtr req);

};

#pragma once

#include <drogon/HttpMiddleware.h>
#include <string>

using namespace drogon;

class ActionTokenMiddleware : public HttpMiddleware<ActionTokenMiddleware>
{
public:
    ActionTokenMiddleware() = default;

    void invoke(const HttpRequestPtr &req,
                MiddlewareNextCallback &&nextCb,
                MiddlewareCallback &&mcb) override;
};

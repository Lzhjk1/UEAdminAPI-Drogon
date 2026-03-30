#pragma once

#include <drogon/HttpFilter.h>
#include <string>

using namespace drogon;

class ActionTokenFilter : public HttpFilter<ActionTokenFilter>
{
public:
    ActionTokenFilter();

    void doFilter(const HttpRequestPtr &req,
                  FilterCallback &&fcb,
                  FilterChainCallback &&fccb) override;
};
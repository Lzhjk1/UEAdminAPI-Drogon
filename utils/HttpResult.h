#pragma once

#include <memory>
#include <string>
#include "json/json.h"
#include "ApiErrorCodes.h"

namespace UEAdminAPI {
    namespace utils {
        class HttpResult {
        public:
            typedef std::shared_ptr<HttpResult> ptr;


            HttpResult(int32_t c = 0, std::string m = "success");
            Json::Value toJson() const;
            std::string toJsonString() const;
            void setResult(int32_t c, std::string m);
            // Overload for ApiErrorCode
            void setResult(ApiErrorCode c);
            void setResult(ApiErrorCode c, const std::string& customMsg);

            int32_t code;
            std::string msg;
            Json::Value jsondata;

        };
    }
}
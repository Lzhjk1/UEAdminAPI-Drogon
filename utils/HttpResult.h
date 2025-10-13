#pragma once

#include <memory>
#include <string>
#include "json/json.h"

namespace UEAdminAPI {
    namespace utils {
        class HttpResult {
        public:
            typedef std::shared_ptr<HttpResult> ptr;


            HttpResult(int32_t c = 0, std::string m = "success");
            std::string toJsonString() const;
            void setResult(int32_t c, std::string m);

            int32_t code;
            std::string msg;
            Json::Value jsondata;

        };
    }
}
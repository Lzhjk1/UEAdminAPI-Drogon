#pragma once

#include <string>

#include "json/json.h"
#include "utils/HttpResult.h"

namespace UEAdminAPI {
namespace Services {

// 客户端环境信息上报：
//   客户端登录成功 --/api/client/env--> 按 (user_id, device_key) 覆盖落库；
//   厂商超级管理员列表查看 / 导出 Markdown。
class ClientEnvService
{
public:
    // body 为客户端提交的完整环境 JSON（含 deviceKey / productVersion）
    static utils::HttpResult Submit(int userId, const Json::Value &body);

    // 厂商：环境上报列表（keyword 为空表示全部）
    static utils::HttpResult AdminList(const std::string &keyword, int page, int size);
};

} // namespace Services
} // namespace UEAdminAPI

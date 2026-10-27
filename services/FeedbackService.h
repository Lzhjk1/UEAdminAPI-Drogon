#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "utils/HttpResult.h"

namespace UEAdminAPI {
namespace Services {

// 用户意见反馈：
//   客户端 --/api/feedback--> 落库，截图落盘（Feedback.StoreDir）
//   厂商超管在登录器里列表查看 / 看图 / 标记已处理
class FeedbackService
{
public:
    // 提交反馈。images 为空表示不带截图；每条反馈最多 5 张，元素为 (mime, 原始字节)。
    static utils::HttpResult Submit(int userId,
                                    int category,
                                    const std::string &content,
                                    const std::string &contact,
                                    const std::string &appVersion,
                                    const std::string &osInfo,
                                    const std::vector<std::pair<std::string, std::string>> &images);

    // 厂商：反馈列表（status < 0 表示全部）
    static utils::HttpResult AdminList(int status, int page, int size);

    // 厂商：读取第 index 张截图（0-based，返回 base64）
    static utils::HttpResult AdminGetImage(int feedbackId, int index);

    // 厂商：标记已处理
    static utils::HttpResult AdminMarkHandled(int feedbackId, int adminUserId);
};

} // namespace Services
} // namespace UEAdminAPI

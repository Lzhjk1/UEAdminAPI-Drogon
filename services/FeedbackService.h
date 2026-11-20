#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "utils/HttpResult.h"

namespace UEAdminAPI {
namespace Services {

// 日志附件：name 为客户端原始文件名，bytes 为解码后的原始内容
struct FeedbackAttachmentData
{
    std::string name;
    std::string mime;
    std::string bytes;
};

// 用户意见反馈：
//   客户端 --/api/feedback--> 落库，截图/日志附件落盘（Feedback.StoreDir）
//   厂商超管在登录器里列表查看 / 看图 / 下载附件 / 标记已处理
class FeedbackService
{
public:
    // 提交反馈。images 为空表示不带截图（最多 5 张，元素为 (mime, 原始字节)）；
    // attachments 为自动附带的崩溃/运行日志（最多 8 个）。
    static utils::HttpResult Submit(int userId,
                                    int category,
                                    const std::string &content,
                                    const std::string &contact,
                                    const std::string &appVersion,
                                    const std::string &osInfo,
                                    const std::vector<std::pair<std::string, std::string>> &images,
                                    const std::vector<FeedbackAttachmentData> &attachments);

    // 厂商：反馈列表（status < 0 表示全部）
    static utils::HttpResult AdminList(int status, int page, int size);

    // 厂商：读取第 index 张截图（0-based，返回 base64）
    static utils::HttpResult AdminGetImage(int feedbackId, int index);

    // 厂商：读取第 index 个日志附件（0-based，返回原始文件名、MIME 与 base64）
    static utils::HttpResult AdminGetAttachment(int feedbackId, int index);

    // 厂商：标记已处理
    static utils::HttpResult AdminMarkHandled(int feedbackId, int adminUserId);
};

} // namespace Services
} // namespace UEAdminAPI

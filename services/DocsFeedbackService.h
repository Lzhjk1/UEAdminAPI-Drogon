#pragma once

#include <string>
#include "utils/HttpResult.h"

namespace UEAdminAPI {
namespace Services {

// 官网文档批注：匿名提交，管理员后台校对
class DocsFeedbackService
{
public:
    static utils::HttpResult Submit(const std::string &pagePath,
                                    const std::string &pageTitle,
                                    const std::string &sectionTitle,
                                    const std::string &selectedText,
                                    const std::string &content,
                                    const std::string &contact,
                                    const std::string &userAgent,
                                    const std::string &clientIp);

    // status < 0 表示全部；status: 0 待校对 / 1 已修正 / 2 忽略
    static utils::HttpResult AdminList(int status, int page, int size);

    static utils::HttpResult AdminHandle(int feedbackId,
                                         int status,
                                         const std::string &note,
                                         int adminUserId);
};

} // namespace Services
} // namespace UEAdminAPI

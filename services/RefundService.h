#pragma once

#include <cstdint>
#include <string>

#include <drogon/utils/coroutine.h>

#include "utils/HttpResult.h"

namespace UEAdminAPI {
namespace Services {

// 充值退款：用户申请 -> 超级管理员审核 -> 渠道退款 -> 扣减余额。
// 规则：整单全额退款；申请与审批时都要求当前余额 >= 订单金额；同一订单同时只允许一条有效申请。
class RefundService
{
public:
    // 用户提交退款申请（仅订单本人）
    static utils::HttpResult Apply(int userId, const std::string &orderNo, const std::string &reason);

    // 超级管理员：退款申请列表 / 详情
    static utils::HttpResult AdminList(int status, const std::string &keyword, int page, int size);
    static utils::HttpResult AdminGet(long long id);

    // 超级管理员：通过（扣余额 + 调渠道退款）/ 驳回 / 同步渠道退款结果
    static drogon::Task<utils::HttpResult> AdminApprove(int adminUserId, long long id, const std::string &remark);
    static utils::HttpResult AdminReject(int adminUserId, long long id, const std::string &remark);
    static drogon::Task<utils::HttpResult> AdminSync(long long id);
    // 卡单兜底：先向渠道确认退款确实失败，才回滚余额并置为失败
    static drogon::Task<utils::HttpResult> AdminRollback(int adminUserId, long long id);

    static std::string StatusText(int status);
};

} // namespace Services
} // namespace UEAdminAPI

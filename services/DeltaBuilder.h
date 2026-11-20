#pragma once

#include <string>
#include "utils/HttpResult.h"

namespace UEAdminAPI {
namespace Services {

// 差量包自动生成：
//   全量包上传完成 -> 后台解包新旧安装器 -> 文件级比对 -> 生成 delta_files.nsh ->
//   makensis(DELTA_MODE) 编译差量安装器 -> 写入 update_delta。
// 生成耗时较长，任务状态记录在 update_delta_job，接口立即返回。
class DeltaBuilder
{
public:
    // 全量包上传完成后触发（异步、串行队列）
    static void EnqueueAutoBuild(int installerId,
                                 const std::string &product = "pdms");

    // 全量包发布后调用：把对应草稿差量置为已发布；还没有差量时补触发一次生成
    static void PublishAutoDelta(const std::string &toVersion,
                                 const std::string &product = "pdms");

    // 全量版本被改名/删除时，清理自动生成的相关差量与文件
    static void RemoveAutoDeltasForVersion(const std::string &version,
                                           const std::string &product = "pdms");

    // 管理端：最近任务列表 / 手动重建
    static utils::HttpResult AdminListJobs(int limit,
                                           const std::string &product = "pdms");
    static utils::HttpResult AdminRebuild(const std::string &fromVersion,
                                          const std::string &toVersion,
                                          const std::string &product = "pdms");

    // 管理端：运行环境自检（7z / makensis / 模板资产 / 临时盘空间）
    static utils::HttpResult AdminHealth();
};

} // namespace Services
} // namespace UEAdminAPI

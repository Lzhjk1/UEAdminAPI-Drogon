/**
 * @file gitlabApi.h
 * @brief GitLab API接口定义文件
 * @details 该文件定义了与GitLab服务器进行交互的API接口，包括用户创建、删除、项目邀请等功能
 * @version 1.0
 * @author UEIMHttpServer团队
 * @date 2025-08-28
 */

#ifndef __UEHTTP_THIRDAPI_GITLAB_H__
#define __UEHTTP_THIRDAPI_GITLAB_H__

#include <string>
#include <unordered_map>
#include "qtch/http/http.h"


namespace UEAdminAPI{
namespace thirdApi{


/**
 * @class GitLabAPI
 * @brief GitLab API操作类
 * @details 提供与GitLab服务器进行交互的静态方法集合，包括用户管理、项目管理等功能
 */
class GitLabAPI{
public:
    /**
     * @enum AccessLevels
     * @brief GitLab访问级别枚举
     * @details 定义了GitLab中不同角色的访问权限级别
     */
    enum AccessLevels{
        NoAccess = 0,           ///< 无访问权限
        MinimalAccess = 5,      ///< 最小访问权限
        Guest = 10,             ///< 访客权限
        Reporter = 20,          ///< 报告者权限
        Developer = 30,         ///< 开发者权限
        Maintainer= 40          ///< 维护者权限
    };

/**
 * @brief 检查管理员配置
 * @details 检查GitLab管理员令牌和API URL是否正确配置
 * @return true 配置正确
 * @return false 配置错误
 */
static bool checkAdminConfig();

/**
 * @brief 生成一个虚拟邮箱
 * 
 * @param userName 用户名
 * @return std::string 生成的虚拟邮箱地址
 */
static std::string generateFakeEmail(const std::string& userName);

/**
 * @brief 在GitLab中创建用户
 * 
 * @param userName [in] 用户名
 * @param password [in] 密码
 * @param email [in] 邮箱，如果为空将生成一个虚拟邮箱
 * @param git_id [out] GitLab用户ID
 * @return true 创建成功
 * @return false 创建失败
 */
static bool createUser(const std::string& userName,const std::string& password, const std::string& email, int32_t& git_id);

/**
 * @brief 设置GitLab API请求的通用头部信息
 * @details 添加Content-Type、PRIVATE-TOKEN和Host头部信息
 * @param req HTTP请求对象指针
 */
static void gitlabApiSetNormalHeaders(qtch::http::HttpRequest::ptr req);

/**
 * @brief 管理员邀请用户加入项目
 * 
 * @param projectId 项目ID
 * @param access_level 访问级别
 * @param git_id GitLab用户ID
 * @return true 邀请成功
 * @return false 邀请失败
 */
static bool adminInvitationProject(int32_t projectId, AccessLevels access_level, int32_t git_id);

/**
 * @brief 删除GitLab用户
 * 
 * @param id GitLab用户ID
 * @return true 删除成功
 * @return false 删除失败
 */
static bool deleteUser(uint32_t id);

/**
 * @brief 检查GitLab用户是否存在
 * 
 * @param id GitLab用户ID
 * @return true 用户存在
 * @return false 用户不存在
 */
static bool isExitedUser(uint32_t id);

/**
 * @brief 修改GitLab用户信息
 * 
 * @param id GitLab用户ID
 * @param datas 要修改的用户信息键值对
 * @return true 修改成功
 * @return false 修改失败
 */
static bool modifyUserInfo(uint32_t id, std::unordered_map<std::string, std::string>& datas);

/**
 * @brief 修改GitLab用户密码
 * 
 * @param id GitLab用户ID
 * @param password 新密码
 * @return true 修改成功
 * @return false 修改失败
 */
static bool modifyUserPassword(uint32_t id, const std::string& password);

/**
 * @brief 创建模拟用户令牌
 * 
 * @param id GitLab用户ID
 * @param impersonationToken [out] 返回的模拟令牌
 * @param impersonationTokenId [out] 返回的令牌ID
 * @return true 创建成功
 * @return false 创建失败
 */
static bool createImpersonationToken(uint32_t id, std::string& impersonationToken, uint32_t& impersonationTokenId);

/**
 * @brief 删除模拟用户令牌
 * 
 * @param id GitLab用户ID
 * @param impersonationTokenId 令牌ID
 * @return true 删除成功
 * @return false 删除失败
 */
static bool deleteImpersonationToken(uint32_t id, uint32_t impersonationTokenId);


};



}







}



#endif
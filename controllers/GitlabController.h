/**
 * @file GitlabController.h
 * @brief GitLab API控制器头文件
 * @details 该文件定义了GitLab相关的HTTP接口控制器
 * @version 1.0
 * @author UEAdminAPI团队
 * @date 2023-08-28
 */

#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

class GitlabController : public drogon::HttpController<GitlabController>
{
public:
    METHOD_LIST_BEGIN
    // 用户管理相关接口
    ADD_METHOD_TO(GitlabController::createUser, "/api/gitlab/user/create", Post);
    ADD_METHOD_TO(GitlabController::deleteUser, "/api/gitlab/user/delete", Post);
    ADD_METHOD_TO(GitlabController::modifyUserPassword, "/api/gitlab/user/password", Post);
    ADD_METHOD_TO(GitlabController::createImpersonationToken, "/api/gitlab/user/token/create", Post);
    ADD_METHOD_TO(GitlabController::deleteImpersonationToken, "/api/gitlab/user/token/delete", Post);

    // 项目管理相关接口
    ADD_METHOD_TO(GitlabController::inviteToProject, "/api/gitlab/project/invite", Post);

    METHOD_LIST_END

    /**
     * @brief 创建GitLab用户
     * @param req HTTP请求对象
     * @return 响应对象
     */
    Task<HttpResponsePtr> createUser(HttpRequestPtr req);

    /**
     * @brief 删除GitLab用户
     * @param req HTTP请求对象
     * @return 响应对象
     */
    Task<HttpResponsePtr> deleteUser(HttpRequestPtr req);

    /**
     * @brief 修改GitLab用户密码
     * @param req HTTP请求对象
     * @return 响应对象
     */
    Task<HttpResponsePtr> modifyUserPassword(HttpRequestPtr req);

    /**
     * @brief 创建GitLab模拟用户令牌
     * @param req HTTP请求对象
     * @return 响应对象
     */
    Task<HttpResponsePtr> createImpersonationToken(HttpRequestPtr req);

    /**
     * @brief 删除GitLab模拟用户令牌
     * @param req HTTP请求对象
     * @return 响应对象
     */
    Task<HttpResponsePtr> deleteImpersonationToken(HttpRequestPtr req);

    /**
     * @brief 邀请用户加入GitLab项目
     * @param req HTTP请求对象
     * @return 响应对象
     */
    Task<HttpResponsePtr> inviteToProject(HttpRequestPtr req);
};

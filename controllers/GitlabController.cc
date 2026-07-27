/**
 * @file GitlabController.cc
 * @brief GitLab API控制器实现文件
 * @details 该文件实现了GitLab相关的HTTP接口控制器
 * @version 1.0
 * @author UEAdminAPI团队
 * @date 2023-08-28
 */

#include "GitlabController.h"
#include "services/GitlabService.h"
#include "utils/PostParamMap.h"
#include <models/User.h>
#include <models/UserGitlabInfo.h>
#include <drogon/orm/Mapper.h>
#include <numeric>

using namespace std;
using namespace drogon;
using namespace drogon::orm;
using namespace drogon_model::UEAdminAPI;
using namespace UEAdminAPI;

Task<HttpResponsePtr> GitlabController::createUser(HttpRequestPtr req) {
    auto reqJson = req->getJsonObject();
    if (!reqJson) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"请求体必须是JSON格式\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    // 使用PostParamMap类处理参数
    PostParamMap paramMap;
    paramMap.addParam("userId", true)
        .addParam("password", true)
        .addParam("email", false);

    paramMap.readParamsFromJson(*reqJson);
    std::vector<std::string> missingFields = paramMap.checkRequiredParams();

    // 如果有缺失的必填项，返回错误
    if (!missingFields.empty()) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"缺少必填项: " + 
                      std::accumulate(missingFields.begin(), missingFields.end(), std::string(), 
                                     [](const std::string& a, const std::string& b) { return a + ", " + b; }) + "\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    // 获取服务实例
    auto gitlabService = GitlabService::Instance();

    // 从数据库获取用户信息
    auto dbClientPtr = drogon::app().getDbClient();
    Mapper<User> mapperUsers(dbClientPtr);

    User user;
    try {
        user = mapperUsers.findFutureOne(Criteria(User::Cols::_id, CompareOperator::EQ, 
                                                 std::stoi(paramMap.getParam("userId")))).get();
    } catch (const DrogonDbException& e) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"用户不存在\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    // 创建GitLab用户
    int32_t gitlabId;
    if (!gitlabService->createUser(
        user.getValueOfName(),
        paramMap.getParam("password"),
        paramMap.getParam("email"),
        gitlabId)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"创建GitLab用户失败\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    // 保存GitLab用户信息到数据库
    Mapper<UserGitlabInfo> mapperGitlabInfo(dbClientPtr);
    UserGitlabInfo gitlabInfo;
    gitlabInfo.setUserId(*user.getId());
    gitlabInfo.setGitlabImpersonationTokenId(0);  // 初始为0，创建令牌后再更新
    gitlabInfo.setGitlabImpersonationToken("");  // 初始为空，创建令牌后再更新

    try {
        mapperGitlabInfo.insertFuture(gitlabInfo).get();
    } catch (const DrogonDbException& e) {
        LOG_ERROR << "保存GitLab用户信息失败: " << e.base().what();
        // 尝试删除已创建的GitLab用户
        gitlabService->deleteUser(gitlabId);
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"保存GitLab用户信息失败\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    auto resp = HttpResponse::newHttpResponse();
    resp->setBody("{\"success\": true, \"gitlabId\": " + std::to_string(gitlabId) + "}");
    resp->setContentTypeCode(CT_APPLICATION_JSON);
    co_return resp;
}

Task<HttpResponsePtr> GitlabController::deleteUser(HttpRequestPtr req) {
    auto reqJson = req->getJsonObject();
    if (!reqJson) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"请求体必须是JSON格式\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    // 使用PostParamMap类处理参数
    PostParamMap paramMap;
    paramMap.addParam("userId", true);

    paramMap.readParamsFromJson(*reqJson);
    std::vector<std::string> missingFields = paramMap.checkRequiredParams();

    // 如果有缺失的必填项，返回错误
    if (!missingFields.empty()) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"缺少必填项: " + 
                      std::accumulate(missingFields.begin(), missingFields.end(), std::string(), 
                                     [](const std::string& a, const std::string& b) { return a + ", " + b; }) + "\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    // 获取服务实例
    auto gitlabService = GitlabService::Instance();

    // 从数据库获取用户GitLab信息
    auto dbClientPtr = drogon::app().getDbClient();
    Mapper<UserGitlabInfo> mapperGitlabInfo(dbClientPtr);

    UserGitlabInfo gitlabInfo;
    try {
        gitlabInfo = mapperGitlabInfo.findFutureOne(Criteria(UserGitlabInfo::Cols::_user_id, CompareOperator::EQ, 
                                                            std::stoi(paramMap.getParam("userId")))).get();
    } catch (const DrogonDbException& e) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"用户GitLab信息不存在\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    // 删除模拟令牌（如果存在）
    if (gitlabInfo.getValueOfGitlabImpersonationTokenId() > 0) {
        gitlabService->deleteImpersonationToken(
            *gitlabInfo.getUserId(),
            gitlabInfo.getValueOfGitlabImpersonationTokenId()
        );
    }

    // 从GitLab删除用户
    if (!gitlabService->deleteUser(*gitlabInfo.getUserId())) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"删除GitLab用户失败\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    // 从数据库删除GitLab用户信息
    try {
        mapperGitlabInfo.deleteFutureBy(Criteria(UserGitlabInfo::Cols::_user_id, CompareOperator::EQ, 
                                               std::stoi(paramMap.getParam("userId")))).get();
    } catch (const DrogonDbException& e) {
        LOG_ERROR << "删除GitLab用户信息失败: " << e.base().what();
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"删除GitLab用户信息失败\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    auto resp = HttpResponse::newHttpResponse();
    resp->setBody("{\"success\": true}");
    resp->setContentTypeCode(CT_APPLICATION_JSON);
    co_return resp;
}

Task<HttpResponsePtr> GitlabController::modifyUserPassword(HttpRequestPtr req) {
    auto reqJson = req->getJsonObject();
    if (!reqJson) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"请求体必须是JSON格式\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    // 使用PostParamMap类处理参数
    PostParamMap paramMap;
    paramMap.addParam("userId", true)
        .addParam("password", true);

    paramMap.readParamsFromJson(*reqJson);
    std::vector<std::string> missingFields = paramMap.checkRequiredParams();

    // 如果有缺失的必填项，返回错误
    if (!missingFields.empty()) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"缺少必填项: " + 
                      std::accumulate(missingFields.begin(), missingFields.end(), std::string(), 
                                     [](const std::string& a, const std::string& b) { return a + ", " + b; }) + "\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    // 获取服务实例
    auto gitlabService = GitlabService::Instance();

    // 从数据库获取用户GitLab信息
    auto dbClientPtr = drogon::app().getDbClient();
    Mapper<UserGitlabInfo> mapperGitlabInfo(dbClientPtr);

    UserGitlabInfo gitlabInfo;
    try {
        gitlabInfo = mapperGitlabInfo.findFutureOne(Criteria(UserGitlabInfo::Cols::_user_id, CompareOperator::EQ, 
                                                            std::stoi(paramMap.getParam("userId")))).get();
    } catch (const DrogonDbException& e) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"用户GitLab信息不存在\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    // 修改GitLab用户密码
    if (!gitlabService->modifyUserPassword(
        *gitlabInfo.getUserId(),
        paramMap.getParam("password"))) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"修改GitLab用户密码失败\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    auto resp = HttpResponse::newHttpResponse();
    resp->setBody("{\"success\": true}");
    resp->setContentTypeCode(CT_APPLICATION_JSON);
    co_return resp;
}

Task<HttpResponsePtr> GitlabController::createImpersonationToken(HttpRequestPtr req) {
    auto reqJson = req->getJsonObject();
    if (!reqJson) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"请求体必须是JSON格式\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    // 使用PostParamMap类处理参数
    PostParamMap paramMap;
    paramMap.addParam("userId", true);

    paramMap.readParamsFromJson(*reqJson);
    std::vector<std::string> missingFields = paramMap.checkRequiredParams();

    // 如果有缺失的必填项，返回错误
    if (!missingFields.empty()) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"缺少必填项: " + 
                      std::accumulate(missingFields.begin(), missingFields.end(), std::string(), 
                                     [](const std::string& a, const std::string& b) { return a + ", " + b; }) + "\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    // 获取服务实例
    auto gitlabService = GitlabService::Instance();

    // 从数据库获取用户GitLab信息
    auto dbClientPtr = drogon::app().getDbClient();
    Mapper<UserGitlabInfo> mapperGitlabInfo(dbClientPtr);

    UserGitlabInfo gitlabInfo;
    try {
        gitlabInfo = mapperGitlabInfo.findFutureOne(Criteria(UserGitlabInfo::Cols::_user_id, CompareOperator::EQ, 
                                                            std::stoi(paramMap.getParam("userId")))).get();
    } catch (const DrogonDbException& e) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"用户GitLab信息不存在\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    // 如果已有令牌，先删除
    if (gitlabInfo.getValueOfGitlabImpersonationTokenId() > 0) {
        gitlabService->deleteImpersonationToken(
            *gitlabInfo.getUserId(),
            gitlabInfo.getValueOfGitlabImpersonationTokenId()
        );
    }

    // 创建新的模拟令牌
    std::string impersonationToken;
    uint32_t impersonationTokenId;
    if (!gitlabService->createImpersonationToken(
        *gitlabInfo.getUserId(),
        impersonationToken,
        impersonationTokenId)) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"创建GitLab模拟令牌失败\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    // 更新数据库中的令牌信息
    gitlabInfo.setGitlabImpersonationTokenId(impersonationTokenId);
    gitlabInfo.setGitlabImpersonationToken(impersonationToken);

    try {
        mapperGitlabInfo.updateFuture(gitlabInfo).get();
    } catch (const DrogonDbException& e) {
        LOG_ERROR << "更新GitLab令牌信息失败: " << e.base().what();
        // 尝试删除刚创建的令牌
        gitlabService->deleteImpersonationToken(*gitlabInfo.getUserId(), impersonationTokenId);
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"更新GitLab令牌信息失败\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    auto resp = HttpResponse::newHttpResponse();
    resp->setBody("{\"success\": true, \"token\": \"" + impersonationToken + "\"}");
    resp->setContentTypeCode(CT_APPLICATION_JSON);
    co_return resp;
}

Task<HttpResponsePtr> GitlabController::deleteImpersonationToken(HttpRequestPtr req) {
    auto reqJson = req->getJsonObject();
    if (!reqJson) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"请求体必须是JSON格式\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    // 使用PostParamMap类处理参数
    PostParamMap paramMap;
    paramMap.addParam("userId", true);

    paramMap.readParamsFromJson(*reqJson);
    std::vector<std::string> missingFields = paramMap.checkRequiredParams();

    // 如果有缺失的必填项，返回错误
    if (!missingFields.empty()) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"缺少必填项: " + 
                      std::accumulate(missingFields.begin(), missingFields.end(), std::string(), 
                                     [](const std::string& a, const std::string& b) { return a + ", " + b; }) + "\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    // 获取服务实例
    auto gitlabService = GitlabService::Instance();

    // 从数据库获取用户GitLab信息
    auto dbClientPtr = drogon::app().getDbClient();
    Mapper<UserGitlabInfo> mapperGitlabInfo(dbClientPtr);

    UserGitlabInfo gitlabInfo;
    try {
        gitlabInfo = mapperGitlabInfo.findFutureOne(Criteria(UserGitlabInfo::Cols::_user_id, CompareOperator::EQ, 
                                                            std::stoi(paramMap.getParam("userId")))).get();
    } catch (const DrogonDbException& e) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"用户GitLab信息不存在\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    // 如果没有令牌，直接返回成功
    if (gitlabInfo.getValueOfGitlabImpersonationTokenId() <= 0) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": true}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    // 删除模拟令牌
    if (!gitlabService->deleteImpersonationToken(
        *gitlabInfo.getUserId(),
        gitlabInfo.getValueOfGitlabImpersonationTokenId())) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"删除GitLab模拟令牌失败\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    // 清空数据库中的令牌信息
    gitlabInfo.setGitlabImpersonationTokenId(0);
    gitlabInfo.setGitlabImpersonationToken("");

    try {
        mapperGitlabInfo.updateFuture(gitlabInfo).get();
    } catch (const DrogonDbException& e) {
        LOG_ERROR << "清空GitLab令牌信息失败: " << e.base().what();
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"清空GitLab令牌信息失败\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    auto resp = HttpResponse::newHttpResponse();
    resp->setBody("{\"success\": true}");
    resp->setContentTypeCode(CT_APPLICATION_JSON);
    co_return resp;
}

Task<HttpResponsePtr> GitlabController::inviteToProject(HttpRequestPtr req) {
    auto reqJson = req->getJsonObject();
    if (!reqJson) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"请求体必须是JSON格式\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    // 使用PostParamMap类处理参数
    PostParamMap paramMap;
    paramMap.addParam("userId", true)
        .addParam("projectId", true)
        .addParam("accessLevel", true);

    paramMap.readParamsFromJson(*reqJson);
    std::vector<std::string> missingFields = paramMap.checkRequiredParams();

    // 如果有缺失的必填项，返回错误
    if (!missingFields.empty()) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"缺少必填项: " + 
                      std::accumulate(missingFields.begin(), missingFields.end(), std::string(), 
                                     [](const std::string& a, const std::string& b) { return a + ", " + b; }) + "\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    // 获取服务实例
    auto gitlabService = GitlabService::Instance();

    // 从数据库获取用户GitLab信息
    auto dbClientPtr = drogon::app().getDbClient();
    Mapper<UserGitlabInfo> mapperGitlabInfo(dbClientPtr);

    UserGitlabInfo gitlabInfo;
    try {
        gitlabInfo = mapperGitlabInfo.findFutureOne(Criteria(UserGitlabInfo::Cols::_user_id, CompareOperator::EQ, 
                                                            std::stoi(paramMap.getParam("userId")))).get();
    } catch (const DrogonDbException& e) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"用户GitLab信息不存在\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    // 解析访问级别
    GitlabService::AccessLevels accessLevel;
    try {
        accessLevel = static_cast<GitlabService::AccessLevels>(std::stoi(paramMap.getParam("accessLevel")));
    } catch (const std::exception& e) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"无效的访问级别\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    // 邀请用户加入项目
    if (!gitlabService->adminInvitationProject(
        std::stoi(paramMap.getParam("projectId")),
        accessLevel,
        *gitlabInfo.getUserId())) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("{\"success\": false, \"message\": \"邀请用户加入GitLab项目失败\"}");
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        co_return resp;
    }

    auto resp = HttpResponse::newHttpResponse();
    resp->setBody("{\"success\": true}");
    resp->setContentTypeCode(CT_APPLICATION_JSON);
    co_return resp;
}

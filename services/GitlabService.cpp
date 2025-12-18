/**
 * @file GitlabService.cpp
 * @brief GitLab API服务类实现文件
 * @details 该文件实现了与GitLab服务器进行交互的服务类，包括用户创建、删除、项目邀请等功能
 * @version 1.0
 * @author UEAdminAPI团队
 * @date 2023-08-28
 */

#include "GitlabService.h"
#include "utils/RandomGenerator.h"
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpClient.h>
#include <trantor/utils/Logger.h>
#include <sstream>

using namespace drogon;
using namespace trantor;

namespace UEAdminAPI {

GitlabService::GitlabService(const Json::Value &config) {
    // 从配置中读取GitLab相关配置
    if (!config.isMember("GitLab") || !config["GitLab"].isObject()) {
        LOG_ERROR << "GitLab配置缺失，请检查配置文件";
        throw std::runtime_error("GitLab配置缺失");
    }

    const auto& gitlabConfig = config["GitLab"];

    // 读取管理员令牌
    if (!gitlabConfig.isMember("AdminToken") || !gitlabConfig["AdminToken"].isString()) {
        LOG_ERROR << "GitLab管理员令牌配置缺失";
        throw std::runtime_error("GitLab管理员令牌配置缺失");
    }
    _gitlabAdminToken = gitlabConfig["AdminToken"].asString();

    // 读取API URL
    if (!gitlabConfig.isMember("ApiHost") || !gitlabConfig["ApiHost"].isString()) {
        LOG_ERROR << "GitLab ApiHost 配置缺失";
        throw std::runtime_error("GitLab ApiHost 配置缺失");
    }
    _gitlabApiHost = gitlabConfig["ApiHost"].asString();

    
    // HttpClient需要https前缀, 而Host Header不能有前缀
    _httpClient = HttpClient::newHttpClient("https://" + _gitlabApiHost);
    _httpClient->setUserAgent("UEAdminAPI/1.0");

    LOG_INFO << "GitLab服务初始化完成，API地址: " << _gitlabApiHost;
}

bool GitlabService::checkAdminConfig() {
    if (_gitlabAdminToken.empty()) {
        LOG_ERROR << "GitLab管理员令牌未配置";
        return false;
    }

    if (!_httpClient) {
        LOG_ERROR << "GitLab HTTP客户端未初始化";
        return false;
    }

    return true;
}

std::string GitlabService::generateFakeEmail(const std::string& userName) {
    if (userName.empty()) {
        LOG_ERROR << "生成虚拟邮箱失败：用户名为空";
        return "";
    }

    std::string code = RandomGenerator::getRandNumberStr(6);
    std::string email = userName + "_" + code + "@email.uesoft.com";

    return email;
}

bool GitlabService::createUser(const std::string& userName, const std::string& password, 
                              const std::string& email, int32_t& git_id) {
    if (!checkAdminConfig()) {
        return false;
    }

    if (userName.empty() || password.empty()) {
        LOG_ERROR << "创建GitLab用户失败：用户名或密码为空";
        return false;
    }

    // 创建请求对象
    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Post);
    req->setPath("/api/v4/users");
    setGitlabApiHeaders(req);

    // 构建请求体
    Json::Value root;
    root["username"] = userName;
    root["name"] = userName;
    root["password"] = password;

    std::string email_final = email.empty() ? generateFakeEmail(userName) : email;
    root["email"] = email_final;
    root["skip_confirmation"] = true;

    req->setBody(root.toStyledString());

    // 发送请求
    auto [result, response] = _httpClient->sendRequest(req);
    if (result != ReqResult::Ok) {
        LOG_ERROR << "创建GitLab用户请求失败: " << result;
        return false;
    }

    if (response->getStatusCode() != k201Created) {
        LOG_ERROR << "创建GitLab用户失败，状态码: " << response->getStatusCode() 
                  << "，响应内容: " << response->getBody();
        return false;
    }

    // 解析响应获取用户ID
    Json::Value responseJson;
    Json::Reader reader;
    if (!reader.parse(std::string(response->getBody()), responseJson)) {
        LOG_ERROR << "解析GitLab创建用户响应失败: " << response->getBody();
        return false;
    }

    if (!responseJson.isMember("id") || !responseJson["id"].isInt()) {
        LOG_ERROR << "GitLab创建用户响应中未找到用户ID";
        return false;
    }

    git_id = responseJson["id"].asInt();
    LOG_INFO << "成功创建GitLab用户，用户名: " << userName 
        << "，邮箱: " << email_final << "，GitLab ID: " << git_id;

    return true;
}

bool GitlabService::adminInvitationProject(int32_t projectId, AccessLevels access_level, int32_t git_id) {
    if (!checkAdminConfig()) {
        return false;
    }

    // 创建请求对象
    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Post);

    std::stringstream ss;
    ss << "/api/v4/projects/" << projectId << "/invitations";
    req->setPath(ss.str());
    setGitlabApiHeaders(req);

    // 构建请求体
    Json::Value root;
    root["user_id"] = std::to_string(git_id);
    root["access_level"] = access_level;

    req->setBody(root.toStyledString());

    // 发送请求
    auto [result, response] = _httpClient->sendRequest(req);
    if (result != ReqResult::Ok) {
        LOG_ERROR << "邀请用户加入GitLab项目请求失败: " << result;
        return false;
    }

    // 解析响应
    Json::Value responseJson;
    Json::Reader reader;
    if (!reader.parse(std::string(response->getBody()), responseJson)) {
        LOG_ERROR << "解析GitLab邀请响应失败: " << response->getBody();
        return false;
    }

    if (!responseJson.isMember("status") || !responseJson["status"].isString()) {
        LOG_ERROR << "GitLab邀请响应中未找到status字段";
        return false;
    }

    if (responseJson["status"].asString() != "success") {
        std::string message = responseJson.isMember("message") ? 
                            responseJson["message"].asString() : "未知错误";
        LOG_ERROR << "邀请用户加入GitLab项目失败: " << message;
        return false;
    }

    LOG_INFO << "成功邀请GitLab用户(ID: " << git_id << ")加入项目(ID: " 
             << projectId << ")，权限级别: " << access_level;

    return true;
}

bool GitlabService::deleteUser(uint32_t id) {
    if (!checkAdminConfig()) {
        return false;
    }

    if (id < 10) {
        LOG_ERROR << "拒绝删除GitLab用户，ID过小: " << id;
        return false;
    }

    // 创建请求对象
    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Delete);

    std::stringstream ss;
    ss << "/api/v4/users/" << id;
    req->setPath(ss.str());
    setGitlabApiHeaders(req);

    // 发送请求
    auto [result, response] = _httpClient->sendRequest(req);
    if (result != ReqResult::Ok) {
        LOG_ERROR << "删除GitLab用户请求失败: " << result;
        return false;
    }

    if (response->getStatusCode() != k204NoContent) {
        LOG_ERROR << "删除GitLab用户失败，状态码: " << response->getStatusCode() 
                  << "，响应内容: " << response->getBody();
        return false;
    }

    LOG_INFO << "成功删除GitLab用户，ID: " << id;
    return true;
}

bool GitlabService::isExitedUser(uint32_t id) {
    if (!checkAdminConfig()) {
        return false;
    }

    if (id < 10) {
        return false;
    }

    // 创建请求对象
    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Get);

    std::stringstream ss;
    ss << "/api/v4/users/" << id;
    req->setPath(ss.str());
    setGitlabApiHeaders(req);

    // 发送请求
    auto [result, response] = _httpClient->sendRequest(req);
    if (result != ReqResult::Ok) {
        LOG_ERROR << "检查GitLab用户存在性请求失败: " << result;
        return false;
    }

    if (response->getStatusCode() != k200OK) {
        LOG_INFO << "GitLab用户不存在，ID: " << id;
        return false;
    }

    LOG_INFO << "GitLab用户存在，ID: " << id;
    return true;
}

bool GitlabService::modifyUserInfo(uint32_t id, std::unordered_map<std::string, std::string>& datas) {
    if (!checkAdminConfig()) {
        return false;
    }

    if (id < 10) {
        LOG_ERROR << "拒绝修改GitLab用户信息，ID过小: " << id;
        return false;
    }

    // 创建请求对象
    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Put);

    std::stringstream ss;
    ss << "/api/v4/users/" << id;
    req->setPath(ss.str());
    setGitlabApiHeaders(req);

    // 构建请求体
    Json::Value root;
    for (const auto& element : datas) {
        root[element.first] = element.second;
    }

    req->setBody(root.toStyledString());

    // 发送请求
    auto [result, response] = _httpClient->sendRequest(req);
    if (result != ReqResult::Ok) {
        LOG_ERROR << "修改GitLab用户信息请求失败: " << result;
        return false;
    }

    if (response->getStatusCode() != k200OK) {
        LOG_ERROR << "修改GitLab用户信息失败，状态码: " << response->getStatusCode() 
                  << "，响应内容: " << response->getBody();
        return false;
    }

    LOG_INFO << "成功修改GitLab用户信息，ID: " << id;
    return true;
}

bool GitlabService::modifyUserPassword(uint32_t id, const std::string& password) {
    std::unordered_map<std::string, std::string> datas;
    datas["password"] = password;
    return modifyUserInfo(id, datas);
}

bool GitlabService::createImpersonationToken(uint32_t id, std::string& impersonationToken, uint32_t& impersonationTokenId) {
    impersonationToken = "";

    if (!checkAdminConfig()) {
        return false;
    }

    if (id < 10) {
        LOG_ERROR << "拒绝为GitLab用户创建模拟令牌，ID过小: " << id;
        return false;
    }

    // 创建请求对象
    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Post);

    std::stringstream ss;
    ss << "/api/v4/users/" << id << "/impersonation_tokens";
    req->setPath(ss.str());
    setGitlabApiHeaders(req);

    // 构建请求体
    Json::Value root;
    root["user_id"] = id;
    root["name"] = "autopdms";
    root["expires_at"] = "2026-11-23";
    root["scopes"].append("api");

    req->setBody(root.toStyledString());

    // 发送请求
    auto [result, response] = _httpClient->sendRequest(req);
    if (result != ReqResult::Ok) {
        LOG_ERROR << "创建GitLab模拟令牌请求失败: " << result;
        return false;
    }

    if (response->getStatusCode() != k201Created) {
        LOG_ERROR << "创建GitLab模拟令牌失败，状态码: " << response->getStatusCode() 
                  << "，响应内容: " << response->getBody();
        return false;
    }

    // 解析响应
    Json::Value responseJson;
    Json::Reader reader;
    if (!reader.parse(std::string(response->getBody()), responseJson)) {
        LOG_ERROR << "解析GitLab模拟令牌响应失败: " << response->getBody();
        return false;
    }

    if (!responseJson.isMember("token") || !responseJson["token"].isString()) {
        LOG_ERROR << "GitLab模拟令牌响应中未找到token字段";
        return false;
    }

    if (!responseJson.isMember("id") || !responseJson["id"].isInt()) {
        LOG_ERROR << "GitLab模拟令牌响应中未找到id字段";
        return false;
    }

    impersonationToken = responseJson["token"].asString();
    impersonationTokenId = responseJson["id"].asInt();

    LOG_INFO << "成功创建GitLab模拟令牌，用户ID: " << id << "，令牌ID: " << impersonationTokenId;
    return true;
}

bool GitlabService::deleteImpersonationToken(uint32_t id, uint32_t impersonationTokenId) {
    if (!checkAdminConfig()) {
        return false;
    }

    if (id < 10) {
        LOG_ERROR << "拒绝删除GitLab模拟令牌，用户ID过小: " << id;
        return false;
    }

    // 创建请求对象
    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Delete);

    std::stringstream ss;
    ss << "/api/v4/users/" << id << "/impersonation_tokens/" << impersonationTokenId;
    req->setPath(ss.str());
    setGitlabApiHeaders(req);

    // 发送请求
    auto [result, response] = _httpClient->sendRequest(req);
    if (result != ReqResult::Ok) {
        LOG_ERROR << "删除GitLab模拟令牌请求失败: " << result;
        return false;
    }

    if (response->getStatusCode() != k204NoContent) {
        LOG_ERROR << "删除GitLab模拟令牌失败，状态码: " << response->getStatusCode() 
                  << "，响应内容: " << response->getBody();
        return false;
    }
    
    LOG_INFO << "成功删除GitLab模拟令牌，用户ID: " << id << "，令牌ID: " << impersonationTokenId;
    return true;
}

void GitlabService::setGitlabApiHeaders(HttpRequestPtr req) {
    if (!req) {
        return;
    }
    req->setContentTypeCode(ContentType::CT_APPLICATION_JSON);
    req->addHeader("PRIVATE-TOKEN", _gitlabAdminToken);
    req->addHeader("Host", _gitlabApiHost);
}

} // namespace UEAdminAPI

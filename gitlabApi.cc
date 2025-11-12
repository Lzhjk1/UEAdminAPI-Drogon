/**
 * @file gitlabApi.cc
 * @brief GitLab API接口实现文件
 * @details 该文件实现了与GitLab服务器进行交互的API接口，包括用户创建、删除、项目邀请等功能
 * @version 1.0
 * @author UEIMHttpServer团队
 * @date 2025-08-28
 */

#include "gitlabApi.h"
#include "qtch/config.h"
#include "qtch/http/http_connection.h"
#include "UEIMHttpServer/util.h"
#include <string>
#include "sstream"
#include <json/json.h>

namespace uehttp {
namespace thirdApi {

static qtch::Logger::ptr logger = QTCH_LOG_NAME("uehttp");
static qtch::ConfigVar<std::string>::ptr s_gitlab_api_admin_token = qtch::Config::LookUp<std::string>("gitlab.api.admin.token","","gitlab api admin token");
static qtch::ConfigVar<std::string>::ptr s_gitlab_api_url = qtch::Config::LookUp<std::string>("gitlab.api.url","","gitlab api url");


static std::string gitlab_admin_token;
static std::string gitlab_api_url;



qtch::http::HttpConnectionPool::ptr gitlabApi_conn_pool = nullptr;

/**
 * @brief GitLab API初始化结构体
 * @details 用于在程序启动时初始化GitLab API相关配置
 */
struct _GitlabApiInit{
    /**
     * @brief 构造函数，初始化GitLab API配置监听器
     */
    _GitlabApiInit(){

        QTCH_CONFIG_VALUE_ADD_LISTENER_STRING(s_gitlab_api_admin_token,gitlab_admin_token);
        QTCH_CONFIG_VALUE_ADD_LISTENER_STRING(s_gitlab_api_url,gitlab_api_url);

        


    }
};
_GitlabApiInit _gitlabApiInit;

/**
 * @brief 生成一个虚拟邮箱
 * @details 根据用户名生成一个格式为 username_randomcode@email.uesoft.com 的虚拟邮箱
 * @param userName 用户名
 * @return std::string 生成的虚拟邮箱地址
 */
std::string GitLabAPI::generateFakeEmail(const std::string& userName){
    if(userName.empty()){
        QTCH_LOG_ERROR(logger) << "generateFakeEmail userName can't be null";
        return "";
    }
    std::string code = uehttp::DataFormatUtil::RandomCode(6);
    std::string email = userName + "_" + code + "@email.uesoft.com";

    return email;
}

/**
 * @brief 检查管理员配置
 * @details 检查GitLab管理员令牌和API URL是否正确配置，如果连接池未初始化则进行初始化
 * @return true 配置正确
 * @return false 配置错误
 */
bool GitLabAPI::checkAdminConfig(){
    if(gitlab_admin_token.empty()){
        QTCH_LOG_ERROR(logger) << "gitlab api config not currect, gitlab.api.admin.token without config";
        return false;
    }
    if(!gitlabApi_conn_pool){
        if(gitlab_api_url.empty()){
            QTCH_LOG_ERROR(logger) << "gitlabe url config not currect, gitlab.api.url without config";
            return false;
        }
        gitlabApi_conn_pool = std::make_shared<qtch::http::HttpConnectionPool>(gitlab_api_url,"",443,true,10,30*1000,20);
    }
    return true;
}

/**
 * @brief 在GitLab中创建用户
 * @details 通过GitLab API创建一个新用户，如果未提供邮箱则自动生成一个虚拟邮箱
 * @param userName [in] 用户名
 * @param password [in] 密码
 * @param email [in] 邮箱，如果为空将生成一个虚拟邮箱
 * @param git_id [out] GitLab用户ID
 * @return true 创建成功
 * @return false 创建失败
 */
bool GitLabAPI::createUser(const std::string& userName,const std::string& password, const std::string& email, int32_t& git_id){
    if(!checkAdminConfig()){
        return false;
    }
    if(userName.empty() || password.empty()){
        return false;
    }
    qtch::http::HttpRequest::ptr req = qtch::http::HttpRequest::ptr(new qtch::http::HttpRequest);
    req->setMethod(qtch::http::HttpMethod::POST);
    gitlabApiSetNormalHeaders(req);

    Json::Value root;
    root["username"] = userName;
    root["name"] = userName;
    root["password"] = password;
    std::string email_final = (email.empty() ? generateFakeEmail(userName) : email);
    root["email"] = email_final;
    root["skip_confirmation"] = true;
    req->setBody(root.toStyledString());

    
    
    qtch::http::HttpResult::ptr httpResult = gitlabApi_conn_pool->DoRequest(req,"https://" + gitlab_api_url + "/api/v4/users",5000);
    if(httpResult->m_result != qtch::http::HttpResult::Error::OK){
        QTCH_LOG_ERROR(logger) << "GitLabAPI::createUser(userName=" << userName << ", email=" << email << ", email_final="<< email_final <<") fail"
            << " errorcode=" << (int)httpResult->m_result
            << " errstr=" << httpResult->m_error;
        return false;
    }
    if(httpResult->m_resonse->getStatus() != qtch::http::HttpStatus::CREATED){
        QTCH_LOG_INFO(logger) << "gitlab create user faile userName=" << userName 
                          << " email=" << email
                          << " statue=" << httpResult->m_resonse->getStatus()
                          << " response=" << httpResult->m_resonse->getBody();
        return false;
    }

    Json::Value value;
    Json::Reader reader;
    if(!reader.parse(httpResult->m_resonse->getBody(),value)){
        QTCH_LOG_ERROR(logger) << "GitLabAPI::createUser"
            << " parse json error \n" << httpResult->m_resonse->getBody();
        return false;
    }
    if(!value["id"].isInt()){
        QTCH_LOG_ERROR(logger) << "GitLabAPI::createUser get id faile";
        return false;
    }
    uint32_t id = value["id"].asInt();
    git_id = id;
    QTCH_LOG_INFO(logger) << "GitLabAPI::createUser(userName=" << userName << ", email=" << email << ", email_final="<< email_final <<") success";
    return true;

}

/**
 * @brief 设置GitLab API请求的通用头部信息
 * @details 添加Content-Type、PRIVATE-TOKEN和Host头部信息
 * @param req HTTP请求对象指针
 */
void GitLabAPI::gitlabApiSetNormalHeaders(qtch::http::HttpRequest::ptr req){
    if(!req){
        return;
    }
    req->setHeader("Content-Type","application/json");
    req->setHeader("PRIVATE-TOKEN",gitlab_admin_token);
    req->setHeader("Host",gitlab_api_url);
}

/**
 * @brief 管理员邀请用户加入项目
 * @details 使用管理员权限邀请用户加入指定项目并设置访问级别
 * @param projectId 项目ID
 * @param access_level 访问级别
 * @param git_id GitLab用户ID
 * @return true 邀请成功
 * @return false 邀请失败
 */
bool GitLabAPI::adminInvitationProject(int32_t projectId, AccessLevels access_level, int32_t git_id){
    if(!checkAdminConfig()){
        return false;
    }
    qtch::http::HttpRequest::ptr req = qtch::http::HttpRequest::ptr(new qtch::http::HttpRequest);
    req->setMethod(qtch::http::HttpMethod::POST);
    gitlabApiSetNormalHeaders(req);
    
    Json::Value root;
    root["user_id"] = std::to_string(git_id);
    root["access_level"] = access_level;

    std::stringstream ss;
    ss << "https://" << gitlab_api_url << "/api/v4/projects/" << projectId << "/invitations";
    
    std::string url = ss.str();
    req->setBody(root.toStyledString());

    qtch::http::HttpResult::ptr httpResult = gitlabApi_conn_pool->DoRequest(req,url,5000);
    if(httpResult->m_result != qtch::http::HttpResult::Error::OK){
        QTCH_LOG_ERROR(logger) << "GitLabAPI::joinProject(projectId=" << projectId << ", access_level=" << access_level << ", git_id="<< git_id <<") fail"
            << " errorcode=" << (int)httpResult->m_result
            << " errstr=" << httpResult->m_error;
        return false;
    }

    Json::Value value;
    Json::Reader reader;
    if(!reader.parse(httpResult->m_resonse->getBody(),value)){
        QTCH_LOG_ERROR(logger) << "http request parse json error \n" << httpResult->m_resonse->getBody();
        return false;
    }
    if(!value["status"].isString()){
        QTCH_LOG_ERROR(logger) << "json can't find member 'status' \n" << httpResult->m_resonse;
        return false;
    }
    if(value["status"].asString()!="success"){
        QTCH_LOG_ERROR(logger) << "gitlab admin Invitation Project faile, reason:{"
            << value["message"] << "}";
        return false;
    }
    QTCH_LOG_INFO(logger) << "gitlab invitation git_id=" << git_id << " project_id=" << projectId << " level=" << access_level;
    return true;
}

/**
 * @brief 删除GitLab用户
 * @details 通过GitLab API删除指定ID的用户
 * @param id GitLab用户ID
 * @return true 删除成功
 * @return false 删除失败
 */
bool GitLabAPI::deleteUser(uint32_t id){
    if(!checkAdminConfig()){
        return false;
    }
    if(id < 10){
        return false;
    }
    qtch::http::HttpRequest::ptr req = qtch::http::HttpRequest::ptr(new qtch::http::HttpRequest);
    req->setMethod(qtch::http::HttpMethod::DELETE);
    gitlabApiSetNormalHeaders(req);

    // Json::Value root;
    // root["hard_delete"] = true;
    // req->setBody(root.toStyledString());
    
    std::stringstream ss;
    ss << "https://" << gitlab_api_url << "/api/v4/users/" << id;
    qtch::http::HttpResult::ptr httpResult = gitlabApi_conn_pool->DoRequest(req,ss.str(),5000);
    if(httpResult->m_result != qtch::http::HttpResult::Error::OK){
        QTCH_LOG_ERROR(logger) << "GitLabAPI::deleteUser(id=" << id << ") fail"
            << " errorcode=" << (int)httpResult->m_result
            << " errstr=" << httpResult->m_error;
        return false;
    }
    if(httpResult->m_resonse->getStatus() != qtch::http::HttpStatus::NO_CONTENT){
        QTCH_LOG_INFO(logger) << "gitlab delete user faile id=" << id 
                          << " statue=" << httpResult->m_resonse->getStatus()
                          << " response=" << httpResult->m_resonse->getBody();
        return false;
    }

    QTCH_LOG_INFO(logger) << "GitLabAPI::delete(id=" << id << ") success";
    return true;
}


/**
 * @brief 检查GitLab用户是否存在
 * @details 通过GitLab API检查指定ID的用户是否存在
 * @param id GitLab用户ID
 * @return true 用户存在
 * @return false 用户不存在或检查失败
 */
bool GitLabAPI::isExitedUser(uint32_t id){
    if(!checkAdminConfig()){
        return false;
    }
    if(id < 10){
        return false;
    }
    qtch::http::HttpRequest::ptr req = qtch::http::HttpRequest::ptr(new qtch::http::HttpRequest);
    req->setMethod(qtch::http::HttpMethod::GET);
    gitlabApiSetNormalHeaders(req);

    // Json::Value root;
    // root["hard_delete"] = true;
    // req->setBody(root.toStyledString());
    
    std::stringstream ss;
    ss << "https://" << gitlab_api_url << "/api/v4/users/" << id;
    qtch::http::HttpResult::ptr httpResult = gitlabApi_conn_pool->DoRequest(req,ss.str(),5000);
    if(httpResult->m_result != qtch::http::HttpResult::Error::OK){
        QTCH_LOG_ERROR(logger) << "GitLabAPI::check(id=" << id << ") fail"
            << " errorcode=" << (int)httpResult->m_result
            << " errstr=" << httpResult->m_error;
        return false;
    }
    if(httpResult->m_resonse->getStatus() != qtch::http::HttpStatus::OK){
        QTCH_LOG_INFO(logger) << "gitlab check user faile id=" << id 
                          << " statue=" << httpResult->m_resonse->getStatus()
                          << " response=" << httpResult->m_resonse->getBody();
        return false;
    }

    QTCH_LOG_INFO(logger) << "GitLabAPI::check(id=" << id << ") success";
    return true;
}

/**
 * @brief 修改GitLab用户信息
 * @details 通过GitLab API修改指定ID用户的信息
 * @param id GitLab用户ID
 * @param datas 要修改的用户信息键值对
 * @return true 修改成功
 * @return false 修改失败
 */
bool GitLabAPI::modifyUserInfo(uint32_t id, std::unordered_map<std::string, std::string>& datas){
    if(!checkAdminConfig()){
        return false;
    }
    if(id < 10){
        return false;
    }
    qtch::http::HttpRequest::ptr req = qtch::http::HttpRequest::ptr(new qtch::http::HttpRequest);
    req->setMethod(qtch::http::HttpMethod::PUT);
    gitlabApiSetNormalHeaders(req);

    // Json::Value root;
    // root["hard_delete"] = true;
    // req->setBody(root.toStyledString());
    
    std::stringstream ss;
    ss << "https://" << gitlab_api_url << "/api/v4/users/" << id;
    
    Json::Value root;
    for(auto& element: datas){
        root[element.first] = element.second;
    }
    std::string body_str = root.toStyledString();
    req->setBody(body_str);

    qtch::http::HttpResult::ptr httpResult = gitlabApi_conn_pool->DoRequest(req,ss.str(),5000);
    if(httpResult->m_result != qtch::http::HttpResult::Error::OK){
        QTCH_LOG_ERROR(logger) << "GitLabAPI::modifyUserInfo(id=" << id << ") fail"
            << " errorcode=" << (int)httpResult->m_result
            << " errstr=" << httpResult->m_error;
        return false;
    }
    if(httpResult->m_resonse->getStatus() != qtch::http::HttpStatus::OK){
        QTCH_LOG_INFO(logger) << "gitlab modify user faile id=" << id 
                          << " statue=" << httpResult->m_resonse->getStatus()
                          << " response=" << httpResult->m_resonse->getBody();
        return false;
    }

    QTCH_LOG_INFO(logger) << "GitLabAPI::modifyUserInfo(id=" << id << ") success";
    return true;
}

/**
 * @brief 修改GitLab用户密码
 * @details 通过GitLab API修改指定ID用户的密码
 * @param id GitLab用户ID
 * @param password 新密码
 * @return true 修改成功
 * @return false 修改失败
 */
bool GitLabAPI::modifyUserPassword(uint32_t id, const std::string& password){
    std::unordered_map<std::string, std::string> datas;
    datas["password"] = password;
    return modifyUserInfo(id, datas);
}

/**
 * @brief 创建模拟用户令牌
 * @details 为指定用户创建一个模拟令牌，用于API访问
 * @param id GitLab用户ID
 * @param impersonationToken [out] 返回的模拟令牌
 * @param impersonationTokenId [out] 返回的令牌ID
 * @return true 创建成功
 * @return false 创建失败
 */
bool GitLabAPI::createImpersonationToken(uint32_t id, std::string& impersonationToken, uint32_t& impersonationTokenId){
    impersonationToken = "";
    if(!checkAdminConfig()){
        return false;
    }
    if(id < 10){
        return false;
    }
    qtch::http::HttpRequest::ptr req = qtch::http::HttpRequest::ptr(new qtch::http::HttpRequest);
    req->setMethod(qtch::http::HttpMethod::POST);
    gitlabApiSetNormalHeaders(req);

    std::stringstream ss;
    ss << "https://" << gitlab_api_url << "/api/v4/users/" << id << "/impersonation_tokens";

    Json::Value root;
    root["user_id"] = id;
    root["name"] = "autopdms";
    root["expires_at"] = "2047-04-01";
    root["scopes"].append("api");


    req->setBody(root.toStyledString());

    qtch::http::HttpResult::ptr httpResult = gitlabApi_conn_pool->DoRequest(req,ss.str(),5000);
    if(httpResult->m_result != qtch::http::HttpResult::Error::OK){
        QTCH_LOG_ERROR(logger) << "GitLabAPI::createImpersonationToken(id=" << id << ") fail"
            << " errorcode=" << (int)httpResult->m_result
            << " errstr=" << httpResult->m_error;
        return false;
    }
    if(httpResult->m_resonse->getStatus() != qtch::http::HttpStatus::CREATED){
        QTCH_LOG_INFO(logger) << "gitlab createImpersonationToken faile id=" << id 
                          << " statue=" << httpResult->m_resonse->getStatus()
                          << " response=" << httpResult->m_resonse->getBody();
        return false;
    }

    Json::Value value;
    Json::Reader reader;
    if(!reader.parse(httpResult->m_resonse->getBody(),value)){
        QTCH_LOG_ERROR(logger) << "http request parse json error \n" << httpResult->m_resonse->getBody();
        return false;
    }
    if(!value["token"].isString()){
        QTCH_LOG_ERROR(logger) << "json can't find member 'token' \n" << httpResult->m_resonse;
        return false;
    }
    if(!value["id"].isInt()){
        QTCH_LOG_ERROR(logger) << "json can't find member 'id' \n" << httpResult->m_resonse;
        return false;
    }
    impersonationToken = value["token"].asString();
    impersonationTokenId = value["id"].asInt();


    QTCH_LOG_INFO(logger) << "GitLabAPI::createImpersonationToken(id=" << id << ") success";
    return true;


}

/**
 * @brief 删除模拟用户令牌
 * @details 删除指定用户的模拟令牌
 * @param id GitLab用户ID
 * @param impersonationTokenId 令牌ID
 * @return true 删除成功
 * @return false 删除失败
 */
bool GitLabAPI::deleteImpersonationToken(uint32_t id, uint32_t impersonationTokenId){
    if(!checkAdminConfig()){
        return false;
    }
    if(id < 10){
        return false;
    }
    qtch::http::HttpRequest::ptr req = qtch::http::HttpRequest::ptr(new qtch::http::HttpRequest);
    req->setMethod(qtch::http::HttpMethod::DELETE);
    gitlabApiSetNormalHeaders(req);

    std::stringstream ss;
    ss << "https://" << gitlab_api_url << "/api/v4/users/" << id << "/impersonation_tokens/" << impersonationTokenId;


    qtch::http::HttpResult::ptr httpResult = gitlabApi_conn_pool->DoRequest(req,ss.str(),5000);
    if(httpResult->m_result != qtch::http::HttpResult::Error::OK){
        QTCH_LOG_ERROR(logger) << "GitLabAPI::deleteImpersonationToken(id=" << id << ") fail"
            << " errorcode=" << (int)httpResult->m_result
            << " errstr=" << httpResult->m_error;
        return false;
    }
    if(httpResult->m_resonse->getStatus() != qtch::http::HttpStatus::NO_CONTENT){
        QTCH_LOG_INFO(logger) << "gitlab deleteImpersonationToken faile id=" << id 
                          << " statue=" << httpResult->m_resonse->getStatus()
                          << " response=" << httpResult->m_resonse->getBody();
        return false;
    }


    QTCH_LOG_INFO(logger) << "GitLabAPI::deleteImpersonationToken(id=" << id << ", impersonationTokenId=" << impersonationTokenId <<") success";
    return true;
}

}
}
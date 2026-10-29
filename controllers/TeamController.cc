#include "TeamController.h"

#include "services/TeamService.h"
#include "utils/ApiErrorCodes.h"
#include "utils/HttpResult.h"
#include "utils/PostParamMap.h"

#include <numeric>
#include <string>
#include <vector>

using namespace drogon;
using namespace UEAdminAPI;
using namespace UEAdminAPI::Services;

namespace {

int CurrentUserId(const HttpRequestPtr &req)
{
    int userId = -1;
    auto attributes = req->getAttributes();
    if (attributes->find("userId"))
    {
        userId = attributes->get<int>("userId");
    }
    return userId;
}

// 读取 roleCodes 数组；老客户端只传单个 roleCode，一并兼容
std::vector<std::string> ReadRoleCodes(const Json::Value &json)
{
    std::vector<std::string> codes;
    if (json.isMember("roleCodes") && json["roleCodes"].isArray())
    {
        for (const auto &v : json["roleCodes"])
        {
            if (v.isString() && !v.asString().empty())
            {
                codes.push_back(v.asString());
            }
        }
    }
    if (codes.empty() && json.isMember("roleCode") && json["roleCode"].isString())
    {
        const std::string legacy = json["roleCode"].asString();
        if (!legacy.empty())
        {
            codes.push_back(legacy);
        }
    }
    return codes;
}

// 系统角色（scope=1）数组；缺省表示老客户端，不改系统角色
std::vector<std::string> ReadSystemRoleCodes(const Json::Value &json)
{
    std::vector<std::string> codes;
    if (json.isMember("systemRoleCodes") && json["systemRoleCodes"].isArray())
    {
        for (const auto &v : json["systemRoleCodes"])
        {
            if (v.isString() && !v.asString().empty())
            {
                codes.push_back(v.asString());
            }
        }
    }
    return codes;
}

} // namespace

Task<HttpResponsePtr> TeamController::getTeam(HttpRequestPtr req)
{
    int userId = -1;
    auto attributes = req->getAttributes();
    if (attributes->find("userId"))
    {
        userId = attributes->get<int>("userId");
    }

    UEAdminAPI::utils::HttpResult result = TeamService::GetTeam(userId);
    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    co_return resp;
}

Task<HttpResponsePtr> TeamController::renameTeam(HttpRequestPtr req)
{
    UEAdminAPI::utils::HttpResult result;

    auto reqJson = req->getJsonObject();
    if (!reqJson)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidJsonFormat);
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k200OK);
        co_return resp;
    }

    const std::string name = reqJson->isMember("name") && (*reqJson)["name"].isString()
                                 ? (*reqJson)["name"].asString()
                                 : "";
    result = TeamService::RenameTeam(CurrentUserId(req), name);
    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    co_return resp;
}

Task<HttpResponsePtr> TeamController::createMember(HttpRequestPtr req)
{
    UEAdminAPI::utils::HttpResult result;

    auto reqJson = req->getJsonObject();
    if (!reqJson)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidJsonFormat);
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k200OK);
        co_return resp;
    }

    PostParamMap pm;
    pm.addParam("userName", true)
      .addParam("password", true)
      .addParam("nickName", false);
    pm.readParamsFromJson(*reqJson);

    const std::vector<std::string> roleCodes = ReadRoleCodes(*reqJson);
    if (roleCodes.empty())
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "缺少必要参数, roleCodes");
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k200OK);
        co_return resp;
    }
    const std::vector<std::string> systemRoleCodes = ReadSystemRoleCodes(*reqJson);

    auto missings = pm.checkRequiredParams();
    if (!missings.empty())
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs,
                         "缺少必要参数" + std::accumulate(missings.begin(), missings.end(), std::string(""),
                                                          [](const std::string &a, const std::string &b) {
                                                              return a + ", " + b;
                                                          }));
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        resp->setStatusCode(k200OK);
        co_return resp;
    }

    int userId = -1;
    auto attributes = req->getAttributes();
    if (attributes->find("userId"))
    {
        userId = attributes->get<int>("userId");
    }

    result = TeamService::CreateMember(userId,
                                       pm.getParam("userName"),
                                       pm.getParam("password"),
                                       roleCodes,
                                       systemRoleCodes,
                                       pm.getParam("nickName"));
    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    co_return resp;
}

Task<HttpResponsePtr> TeamController::removeMember(HttpRequestPtr req, std::string userId)
{
    UEAdminAPI::utils::HttpResult result;

    int memberUserId = 0;
    try
    {
        memberUserId = std::stoi(userId);
    }
    catch (...)
    {
        memberUserId = 0;
    }
    if (memberUserId <= 0)
    {
        result.setResult(ApiErrorCode::ApiError_UserNotFound);
        co_return HttpResponse::newHttpJsonResponse(result.toJson());
    }

    result = TeamService::RemoveMember(CurrentUserId(req), memberUserId);
    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    co_return resp;
}

Task<HttpResponsePtr> TeamController::updateMemberRoles(HttpRequestPtr req, std::string userId)
{
    UEAdminAPI::utils::HttpResult result;

    int memberUserId = 0;
    try
    {
        memberUserId = std::stoi(userId);
    }
    catch (...)
    {
        memberUserId = 0;
    }
    if (memberUserId <= 0)
    {
        result.setResult(ApiErrorCode::ApiError_UserNotFound);
        co_return HttpResponse::newHttpJsonResponse(result.toJson());
    }

    auto reqJson = req->getJsonObject();
    if (!reqJson)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidJsonFormat);
        co_return HttpResponse::newHttpJsonResponse(result.toJson());
    }

    const std::vector<std::string> roleCodes = ReadRoleCodes(*reqJson);
    if (roleCodes.empty())
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "至少保留一个团队角色");
        co_return HttpResponse::newHttpJsonResponse(result.toJson());
    }
    const bool updateSystemRoles = reqJson->isMember("systemRoleCodes");
    const std::vector<std::string> systemRoleCodes = ReadSystemRoleCodes(*reqJson);

    int operatorUserId = -1;
    auto attributes = req->getAttributes();
    if (attributes->find("userId"))
    {
        operatorUserId = attributes->get<int>("userId");
    }

    result = TeamService::UpdateMemberRoles(operatorUserId, memberUserId, roleCodes,
                                            systemRoleCodes, updateSystemRoles);
    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    co_return resp;
}

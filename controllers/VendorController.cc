#include "VendorController.h"

#include "services/TeamService.h"
#include "utils/ApiErrorCodes.h"
#include "utils/HttpResult.h"

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

int ParseId(const std::string &text)
{
    try
    {
        return std::stoi(text);
    }
    catch (...)
    {
        return 0;
    }
}

HttpResponsePtr Respond(const UEAdminAPI::utils::HttpResult &result)
{
    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    return resp;
}

std::vector<std::string> ReadStringArray(const Json::Value &json, const char *key)
{
    std::vector<std::string> values;
    if (json.isMember(key) && json[key].isArray())
    {
        for (const auto &v : json[key])
        {
            if (v.isString() && !v.asString().empty())
            {
                values.push_back(v.asString());
            }
        }
    }
    return values;
}

std::vector<int> ReadIntArray(const Json::Value &json, const char *key)
{
    std::vector<int> values;
    if (!json.isMember(key) || !json[key].isArray())
    {
        return values;
    }
    for (const auto &v : json[key])
    {
        if (v.isInt())
        {
            values.push_back(v.asInt());
        }
        else if (v.isString())
        {
            try
            {
                values.push_back(std::stoi(v.asString()));
            }
            catch (...)
            {
            }
        }
    }
    return values;
}

HttpResponsePtr RespondBadJson(const char *message)
{
    UEAdminAPI::utils::HttpResult result;
    result.setResult(ApiErrorCode::ApiError_InvalidJsonFormat, message);
    return Respond(result);
}

} // namespace

Task<HttpResponsePtr> VendorController::listTeams(HttpRequestPtr req)
{
    co_return Respond(TeamService::VendorListTeams(CurrentUserId(req)));
}

Task<HttpResponsePtr> VendorController::getTeam(HttpRequestPtr req, std::string teamId)
{
    const int id = ParseId(teamId);
    if (id <= 0)
    {
        UEAdminAPI::utils::HttpResult result;
        result.setResult(ApiErrorCode::ApiError_TeamNotFound);
        co_return Respond(result);
    }
    co_return Respond(TeamService::VendorGetTeam(CurrentUserId(req), id));
}

Task<HttpResponsePtr> VendorController::createMember(HttpRequestPtr req, std::string teamId)
{
    const int id = ParseId(teamId);
    if (id <= 0)
    {
        UEAdminAPI::utils::HttpResult result;
        result.setResult(ApiErrorCode::ApiError_TeamNotFound);
        co_return Respond(result);
    }

    auto reqJson = req->getJsonObject();
    if (!reqJson)
    {
        co_return RespondBadJson("请求体必须是 JSON");
    }

    const std::string userName = reqJson->isMember("userName") && (*reqJson)["userName"].isString()
                                     ? (*reqJson)["userName"].asString()
                                     : "";
    const std::string password = reqJson->isMember("password") && (*reqJson)["password"].isString()
                                     ? (*reqJson)["password"].asString()
                                     : "";
    const std::string nickName = reqJson->isMember("nickName") && (*reqJson)["nickName"].isString()
                                     ? (*reqJson)["nickName"].asString()
                                     : "";
    const std::vector<std::string> roleCodes = ReadStringArray(*reqJson, "roleCodes");
    const std::vector<std::string> systemRoleCodes = ReadStringArray(*reqJson, "systemRoleCodes");

    co_return Respond(TeamService::VendorCreateMember(CurrentUserId(req), id, userName, password,
                                                      roleCodes, systemRoleCodes, nickName));
}

Task<HttpResponsePtr> VendorController::updateMember(HttpRequestPtr req,
                                                     std::string teamId,
                                                     std::string userId)
{
    const int id = ParseId(teamId);
    const int memberUserId = ParseId(userId);
    if (id <= 0)
    {
        UEAdminAPI::utils::HttpResult result;
        result.setResult(ApiErrorCode::ApiError_TeamNotFound);
        co_return Respond(result);
    }
    if (memberUserId <= 0)
    {
        UEAdminAPI::utils::HttpResult result;
        result.setResult(ApiErrorCode::ApiError_UserNotFound);
        co_return Respond(result);
    }

    auto reqJson = req->getJsonObject();
    if (!reqJson)
    {
        co_return RespondBadJson("请求体必须是 JSON");
    }

    const std::vector<std::string> roleCodes = ReadStringArray(*reqJson, "roleCodes");
    if (roleCodes.empty())
    {
        UEAdminAPI::utils::HttpResult result;
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "至少保留一个团队角色");
        co_return Respond(result);
    }
    const bool updateSystemRoles = reqJson->isMember("systemRoleCodes");
    const std::vector<std::string> systemRoleCodes = ReadStringArray(*reqJson, "systemRoleCodes");

    co_return Respond(TeamService::VendorUpdateMemberRoles(
        CurrentUserId(req), id, memberUserId, roleCodes, systemRoleCodes, updateSystemRoles));
}

Task<HttpResponsePtr> VendorController::removeMember(HttpRequestPtr req,
                                                     std::string teamId,
                                                     std::string userId)
{
    const int id = ParseId(teamId);
    const int memberUserId = ParseId(userId);
    if (id <= 0)
    {
        UEAdminAPI::utils::HttpResult result;
        result.setResult(ApiErrorCode::ApiError_TeamNotFound);
        co_return Respond(result);
    }
    if (memberUserId <= 0)
    {
        UEAdminAPI::utils::HttpResult result;
        result.setResult(ApiErrorCode::ApiError_UserNotFound);
        co_return Respond(result);
    }
    co_return Respond(TeamService::VendorRemoveMember(CurrentUserId(req), id, memberUserId));
}

Task<HttpResponsePtr> VendorController::moveMembers(HttpRequestPtr req, std::string teamId)
{
    const int id = ParseId(teamId);
    if (id <= 0)
    {
        UEAdminAPI::utils::HttpResult result;
        result.setResult(ApiErrorCode::ApiError_TeamNotFound);
        co_return Respond(result);
    }

    auto reqJson = req->getJsonObject();
    if (!reqJson)
    {
        co_return RespondBadJson("请求体必须是 JSON");
    }

    const std::vector<int> userIds = ReadIntArray(*reqJson, "userIds");
    const std::vector<std::string> roleCodes = ReadStringArray(*reqJson, "roleCodes");
    if (userIds.empty())
    {
        UEAdminAPI::utils::HttpResult result;
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "请选择要移动的用户");
        co_return Respond(result);
    }

    co_return Respond(TeamService::VendorMoveMembers(CurrentUserId(req), id, userIds, roleCodes));
}

Task<HttpResponsePtr> VendorController::listCandidates(HttpRequestPtr req)
{
    const std::string keyword = req->getParameter("keyword");
    int excludeTeamId = 0;
    if (!req->getParameter("excludeTeamId").empty())
    {
        excludeTeamId = ParseId(req->getParameter("excludeTeamId"));
    }
    int limit = 50;
    if (!req->getParameter("limit").empty())
    {
        limit = ParseId(req->getParameter("limit"));
    }
    co_return Respond(TeamService::VendorListCandidates(CurrentUserId(req), keyword,
                                                        excludeTeamId, limit));
}

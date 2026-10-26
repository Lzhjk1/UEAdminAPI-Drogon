#include "TeamController.h"

#include "services/TeamService.h"
#include "utils/ApiErrorCodes.h"
#include "utils/HttpResult.h"
#include "utils/PostParamMap.h"

#include <numeric>

using namespace drogon;
using namespace UEAdminAPI;
using namespace UEAdminAPI::Services;

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
      .addParam("roleCode", true)
      .addParam("nickName", false);
    pm.readParamsFromJson(*reqJson);

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
                                       pm.getParam("roleCode"),
                                       pm.getParam("nickName"));
    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    co_return resp;
}

#include "MeController.h"

#include "services/TeamService.h"
#include "utils/HttpResult.h"

using namespace drogon;
using namespace UEAdminAPI;
using namespace UEAdminAPI::Services;

Task<HttpResponsePtr> MeController::bootstrap(HttpRequestPtr req)
{
    int userId = -1;
    auto attributes = req->getAttributes();
    if (attributes->find("userId"))
    {
        userId = attributes->get<int>("userId");
    }

    UEAdminAPI::utils::HttpResult result = TeamService::GetBootstrap(userId);
    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    co_return resp;
}

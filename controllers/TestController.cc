#include "TestController.h"
#include "services/GitlabService.h"
#include "utils/HttpResult.h"

using namespace drogon;
using namespace UEAdminAPI;
using namespace UEAdminAPI::utils;

Task<HttpResponsePtr> TestController::TestHandler(HttpRequestPtr req) {
    auto _gitlabService = GitlabService::Instance();

    HttpResult result;

    // int userGitId = -1;
    // bool isSuccess = _gitlabService->createUser("roasalGitTest2", "Porthack123", "everspaceGit2@qq.com", userGitId);

    // _gitlabService->adminInvitationProject(1, UEAdminAPI::GitlabService::AccessLevels::Developer, 50);

    std::string gitlabImpersonationToken;
    uint32_t gitImpersonationTokenId = -1;
    _gitlabService->createImpersonationToken(50, gitlabImpersonationToken, gitImpersonationTokenId);

    // bool isDeleteSuccess = _gitlabService->deleteImpersonationToken(50, gitImpersonationTokenId);

    // auto result = co_await _gitlabService->TestFunc();

    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    co_return resp;
}

#include "TestController.h"
#include "drogon/orm/DbClient.h"
#include <string>
#include <vector>
#include "utils/HttpResult.h"

#include "services/GitlabService.h"

#include <plugins/SMTPMail.h>
#include "utils/GetAnotherIoLoop.h"
#include "services/ServiceDtos.h"

using namespace drogon;
using namespace drogon::orm;
// using namespace drogon_model::UEAdminAPI;
using namespace UEAdminAPI;
using namespace UEAdminAPI::utils;
using namespace ServiceDtos;

TestController::TestController() {

}

Task<HttpResponsePtr> TestController::TestHandler(HttpRequestPtr req) {
    auto _gitlabService = GitlabService::Instance();

	auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    HttpResult result;

    // int userGitId = -1;
    // bool isSuccess = _gitlabService->createUser("roasalGitTest2", "Porthack123", "everspaceGit2@qq.com", userGitId);

    // _gitlabService->adminInvitationProject(1, UEAdminAPI::GitlabService::AccessLevels::Developer, 50);

    std::string gitlabImpersonationToken;
    uint32_t gitImpersonationTokenId = -1;
    _gitlabService->createImpersonationToken(50, gitlabImpersonationToken, gitImpersonationTokenId);

	co_return resp;
}

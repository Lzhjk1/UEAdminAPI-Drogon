#include "UpdateController.h"

#include "services/UpdateService.h"
#include "utils/ApiErrorCodes.h"
#include "utils/HttpResult.h"

#include <drogon/MultiPart.h>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace drogon;
using namespace UEAdminAPI;
using namespace UEAdminAPI::Services;
using namespace UEAdminAPI::utils;

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

HttpResponsePtr JsonResponse(const HttpResult &result)
{
    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    resp->setStatusCode(k200OK);
    return resp;
}

int ParseId(const std::string &value)
{
    try
    {
        return std::stoi(value);
    }
    catch (...)
    {
        return 0;
    }
}

} // namespace

Task<HttpResponsePtr> UpdateController::check(HttpRequestPtr req)
{
    const std::string currentVersion = req->getParameter("currentVersion");
    const std::string platform = req->getParameter("platform");

    HttpResult result;
    if (currentVersion.empty())
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "缺少 currentVersion");
        co_return JsonResponse(result);
    }
    co_return JsonResponse(UpdateService::CheckUpdate(CurrentUserId(req), currentVersion, platform));
}

Task<HttpResponsePtr> UpdateController::download(HttpRequestPtr req, std::string version)
{
    HttpResult error;
    std::string path;
    std::string fileName;
    if (!UpdateService::ResolveDownload(CurrentUserId(req), version, path, fileName, error))
    {
        co_return JsonResponse(error);
    }

    auto resp = HttpResponse::newFileResponse(path);
    resp->setStatusCode(k200OK);
    resp->addHeader("Content-Disposition", "attachment; filename=\"" + fileName + "\"");
    co_return resp;
}

Task<HttpResponsePtr> UpdateController::upload(HttpRequestPtr req)
{
    HttpResult result;

    MultiPartParser parser;
    if (parser.parse(req) != 0)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "请使用 multipart/form-data 上传");
        co_return JsonResponse(result);
    }

    const auto &params = parser.getParameters();
    const auto versionIt = params.find("version");
    if (versionIt == params.end() || versionIt->second.empty())
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "缺少 version");
        co_return JsonResponse(result);
    }
    const std::string version = versionIt->second;
    const std::string notes = params.count("notes") ? params.at("notes") : "";
    int buildNo = 0;
    if (params.count("buildNo"))
    {
        buildNo = ParseId(params.at("buildNo"));
    }

    const auto &files = parser.getFiles();
    if (files.empty())
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "缺少安装包文件");
        co_return JsonResponse(result);
    }
    const auto &file = files[0];
    co_return JsonResponse(UpdateService::AdminUploadInstaller(
        version, notes, buildNo, file.getFileName(), file.fileContent()));
}

Task<HttpResponsePtr> UpdateController::publish(HttpRequestPtr req, std::string installerId)
{
    (void)req;
    HttpResult result;
    const int id = ParseId(installerId);
    if (id <= 0)
    {
        result.setResult(ApiErrorCode::ApiError_InstallerNotFound);
        co_return JsonResponse(result);
    }
    co_return JsonResponse(UpdateService::AdminPublishInstaller(id));
}

Task<HttpResponsePtr> UpdateController::list(HttpRequestPtr req)
{
    (void)req;
    co_return JsonResponse(UpdateService::AdminListInstallers());
}

Task<HttpResponsePtr> UpdateController::adminPage(HttpRequestPtr req)
{
    (void)req;
    // 页面在源码 views/ 下；服务以仓库根目录启动时可直接找到
    const std::string pagePath = "./views/installer_admin.html";
    std::error_code ec;
    if (!std::filesystem::exists(pagePath, ec))
    {
        HttpResult result;
        result.setResult(ApiErrorCode::ApiError_InstallerNotFound, "上传页不存在: " + pagePath);
        co_return JsonResponse(result);
    }

    std::ifstream ifs(pagePath, std::ios::binary);
    std::ostringstream oss;
    oss << ifs.rdbuf();

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    resp->setContentTypeCode(CT_TEXT_HTML);
    resp->setBody(oss.str());
    // 不设 charset 时浏览器按 UTF-8 解析，页面内已用 meta 声明
    co_return resp;
}

#include "UpdateController.h"

#include "services/UpdateService.h"
#include "utils/ApiErrorCodes.h"
#include "utils/HttpResult.h"

#include <drogon/MultiPart.h>

#include <cctype>
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

// 从安装包文件名解析版本号与构建号：
//   UEAP_PRJ_11.7.120_Setup.Win32.build_202610271350.exe
// buildNo 返回 0 表示没解析出来（build_no 是 32 位整数，12 位时间戳放不下）。
bool ParseInstallerFileName(const std::string &fileName, std::string &version, int &buildNo)
{
    std::string name = fileName;
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos)
    {
        name = name.substr(slash + 1);
    }

    auto pos = name.find("UEAP_PRJ_");
    if (pos == std::string::npos)
    {
        return false;
    }
    pos += 9;

    std::string ver;
    while (pos < name.size() &&
           (std::isdigit(static_cast<unsigned char>(name[pos])) || name[pos] == '.'))
    {
        ver += name[pos++];
    }
    if (ver.empty() || ver.find('.') == std::string::npos || ver.front() == '.' ||
        ver.back() == '.' || ver.find("..") != std::string::npos)
    {
        return false;
    }
    version = ver;

    buildNo = 0;
    auto build = name.find("build_", pos);
    if (build != std::string::npos)
    {
        build += 6;
        std::string num;
        while (build < name.size() && std::isdigit(static_cast<unsigned char>(name[build])))
        {
            num += name[build++];
        }
        if (!num.empty() && num.size() <= 9)
        {
            buildNo = std::stoi(num);
        }
    }
    return true;
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
    const std::string notes = params.count("notes") ? params.at("notes") : "";
    const std::string minVersion = params.count("minVersion") ? params.at("minVersion") : "";
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

    // 版本号没填时按打包命名解析，覆盖直接调接口、不经过两个上传界面的调用方
    std::string version = (versionIt == params.end()) ? std::string() : versionIt->second;
    if (version.empty() || buildNo == 0)
    {
        std::string parsedVersion;
        int parsedBuildNo = 0;
        if (ParseInstallerFileName(file.getFileName(), parsedVersion, parsedBuildNo))
        {
            if (version.empty())
            {
                version = parsedVersion;
            }
            if (buildNo == 0)
            {
                buildNo = parsedBuildNo;
            }
        }
    }
    if (version.empty())
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs,
                         "缺少 version（文件名不符合 UEAP_PRJ_<版本>_Setup... 约定）");
        co_return JsonResponse(result);
    }

    co_return JsonResponse(UpdateService::AdminUploadInstaller(
        version, notes, buildNo, minVersion, file.getFileName(), file.fileContent()));
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

Task<HttpResponsePtr> UpdateController::offline(HttpRequestPtr req, std::string installerId)
{
    (void)req;
    HttpResult result;
    const int id = ParseId(installerId);
    if (id <= 0)
    {
        result.setResult(ApiErrorCode::ApiError_InstallerNotFound);
        co_return JsonResponse(result);
    }
    co_return JsonResponse(UpdateService::AdminOfflineInstaller(id));
}

Task<HttpResponsePtr> UpdateController::update(HttpRequestPtr req, std::string installerId)
{
    HttpResult result;
    const int id = ParseId(installerId);
    if (id <= 0)
    {
        result.setResult(ApiErrorCode::ApiError_InstallerNotFound);
        co_return JsonResponse(result);
    }

    auto reqJson = req->getJsonObject();
    if (!reqJson)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidJsonFormat);
        co_return JsonResponse(result);
    }
    if (!reqJson->isMember("version") || !(*reqJson)["version"].isString())
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "缺少 version");
        co_return JsonResponse(result);
    }

    const std::string version = (*reqJson)["version"].asString();
    int buildNo = 0;
    if (reqJson->isMember("buildNo"))
    {
        const auto &node = (*reqJson)["buildNo"];
        try
        {
            buildNo = node.isInt() ? node.asInt() : std::stoi(node.asString());
        }
        catch (...)
        {
            buildNo = -1; // 交给服务层报「构建号不合法」
        }
    }
    const std::string notes =
        (reqJson->isMember("notes") && (*reqJson)["notes"].isString())
            ? (*reqJson)["notes"].asString() : "";
    const std::string minVersion =
        (reqJson->isMember("minVersion") && (*reqJson)["minVersion"].isString())
            ? (*reqJson)["minVersion"].asString() : "";

    co_return JsonResponse(UpdateService::AdminUpdateInstaller(
        id, version, buildNo, notes, minVersion));
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

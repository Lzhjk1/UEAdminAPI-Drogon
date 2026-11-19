#include "UpdateController.h"

#include "services/UpdateService.h"
#include "services/DeltaBuilder.h"
#include "utils/ApiErrorCodes.h"
#include "utils/HttpResult.h"

#include <drogon/MultiPart.h>

#include <cctype>

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

long long ParseInt64(const std::string &value, long long fallback)
{
    try
    {
        return std::stoll(value);
    }
    catch (...)
    {
        return fallback;
    }
}

constexpr size_t kMaxChunkBytes = 8 * 1024 * 1024;

std::string JsonString(const Json::Value &json, const char *key)
{
    if (json.isMember(key) && json[key].isString())
    {
        return json[key].asString();
    }
    return std::string();
}

int64_t JsonInt64(const Json::Value &json, const char *key, int64_t fallback)
{
    if (!json.isMember(key))
    {
        return fallback;
    }
    const auto &node = json[key];
    if (node.isInt64())
    {
        return node.asInt64();
    }
    if (node.isString())
    {
        return ParseInt64(node.asString(), fallback);
    }
    return fallback;
}

// 从安装包文件名解析版本号与构建号：
//   UEAP_PRJ_11.7.120_Setup.Win32.build_202610271350.exe
// buildNo 返回 0 表示没解析出来；支持文件名里的 build- 与 build_ 两种分隔符。
bool ParseInstallerFileName(const std::string &fileName, std::string &version, long long &buildNo)
{
    std::string name = fileName;
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos)
    {
        name = name.substr(slash + 1);
    }

    bool found = false;

    // 版本号：UEAP_PRJ_<major>.<minor>.<patch>_...
    auto pos = name.find("UEAP_PRJ_");
    if (pos != std::string::npos)
    {
        pos += 9;
        std::string ver;
        while (pos < name.size() &&
               (std::isdigit(static_cast<unsigned char>(name[pos])) || name[pos] == '.'))
        {
            ver += name[pos++];
        }
        if (!ver.empty() && ver.find('.') != std::string::npos && ver.front() != '.' &&
            ver.back() != '.' && ver.find("..") == std::string::npos)
        {
            version = ver;
            found = true;
        }
    }

    // 构建号：build_ / build- 后面的数字（yyyyMMddHHmm 共 12 位，bigint 放得下）
    buildNo = 0;
    auto build = name.find("build_");
    if (build == std::string::npos)
    {
        build = name.find("build-");
    }
    if (build != std::string::npos)
    {
        build += 6;
        std::string num;
        while (build < name.size() && std::isdigit(static_cast<unsigned char>(name[build])))
        {
            num += name[build++];
        }
        if (!num.empty() && num.size() <= 18)
        {
            try
            {
                buildNo = std::stoll(num);
                found = true;
            }
            catch (...)
            {
                buildNo = 0;
            }
        }
    }
    return found;
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
    const std::string kind = req->getParameter("kind");
    const std::string fromVersion = req->getParameter("fromVersion");
    if (!UpdateService::ResolveDownload(CurrentUserId(req), version, path, fileName, error,
                                        kind, fromVersion))
    {
        co_return JsonResponse(error);
    }

    auto resp = HttpResponse::newFileResponse(path);
    resp->setStatusCode(k200OK);
    resp->addHeader("Content-Disposition", "attachment; filename=\"" + fileName + "\"");
    co_return resp;
}

Task<HttpResponsePtr> UpdateController::publicReleases(HttpRequestPtr req)
{
    int limit = 2;
    const std::string value = req->getParameter("limit");
    if (!value.empty())
    {
        try
        {
            limit = std::stoi(value);
        }
        catch (...)
        {
            limit = 2;
        }
    }
    if (limit < 1 || limit > 10)
    {
        limit = 2;
    }
    co_return JsonResponse(UpdateService::PublicReleases(limit));
}

Task<HttpResponsePtr> UpdateController::publicDownload(HttpRequestPtr req, std::string version)
{
    std::string fileName;
    HttpResult result = UpdateService::PublicDownloadInfo(version, fileName);
    if (result.code != 0)
    {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k404NotFound);
        resp->setContentTypeCode(CT_TEXT_PLAIN);
        resp->setBody(result.msg);
        co_return resp;
    }

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    resp->setContentTypeCode(CT_APPLICATION_OCTET_STREAM);
    resp->addHeader("Content-Disposition", "attachment; filename=\"" + fileName + "\"");
    resp->addHeader("X-Accel-Redirect", "/__installers/" + version + "/" + fileName);
    co_return resp;
}

Task<HttpResponsePtr> UpdateController::deltaManifest(HttpRequestPtr req, std::string version)
{
    const std::string fromVersion = req->getParameter("fromVersion");
    HttpResult result;
    Json::Value manifest;
    if (!UpdateService::ResolveDeltaManifest(CurrentUserId(req), version, fromVersion, manifest, result))
    {
        co_return JsonResponse(result);
    }
    result.jsondata = manifest;
    result.setResult(ApiErrorCode::ApiError_Success, "success");
    co_return JsonResponse(result);
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
    long long buildNo = 0;
    if (params.count("buildNo"))
    {
        buildNo = ParseInt64(params.at("buildNo"), 0);
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
        long long parsedBuildNo = 0;
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

Task<HttpResponsePtr> UpdateController::uploadInit(HttpRequestPtr req)
{
    HttpResult result;
    auto json = req->getJsonObject();
    if (!json)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidJsonFormat);
        co_return JsonResponse(result);
    }

    const std::string version = JsonString(*json, "version");
    const std::string notes = JsonString(*json, "notes");
    const std::string minVersion = JsonString(*json, "minVersion");
    const std::string fileName = JsonString(*json, "fileName");
    const std::string fileSha256 = JsonString(*json, "fileSha256");
    const std::string kind = JsonString(*json, "kind");
    const std::string fromVersion = JsonString(*json, "fromVersion");
    if (version.empty() || fileName.empty())
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "缺少 version / fileName");
        co_return JsonResponse(result);
    }

    // manifest 允许直接给对象，也允许给 JSON 字符串（部分脚本从文件读出来再塞进来）
    Json::Value manifest(Json::objectValue);
    if (json->isMember("manifest"))
    {
        const auto &node = (*json)["manifest"];
        if (node.isObject())
        {
            manifest = node;
        }
        else if (node.isString())
        {
            Json::CharReaderBuilder builder;
            std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
            std::string errors;
            const std::string text = node.asString();
            if (!reader->parse(text.data(), text.data() + text.size(), &manifest, &errors))
            {
                result.setResult(ApiErrorCode::ApiError_InvalidJsonFormat, "manifest 不是合法 JSON");
                co_return JsonResponse(result);
            }
        }
    }

    co_return JsonResponse(UpdateService::AdminInitUpload(
        CurrentUserId(req), version, JsonInt64(*json, "buildNo", 0), notes, minVersion,
        fileName, JsonInt64(*json, "fileSize", 0), fileSha256, kind, fromVersion, manifest));
}

Task<HttpResponsePtr> UpdateController::uploadChunk(HttpRequestPtr req, std::string uploadId)
{
    HttpResult result;
    const int64_t offset = ParseInt64(req->getParameter("offset"), -1);
    const auto body = req->getBody();
    if (offset < 0)
    {
        result.setResult(ApiErrorCode::ApiError_MissingRequiredArgs, "缺少 offset");
        co_return JsonResponse(result);
    }
    if (body.empty() || body.size() > kMaxChunkBytes)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidOperation, "分片为空或过大");
        co_return JsonResponse(result);
    }

    co_return JsonResponse(UpdateService::AdminUploadChunk(
        CurrentUserId(req), uploadId, offset,
        std::string_view(body.data(), body.size())));
}

Task<HttpResponsePtr> UpdateController::uploadComplete(HttpRequestPtr req, std::string uploadId)
{
    (void)req;
    co_return JsonResponse(UpdateService::AdminCompleteUpload(CurrentUserId(req), uploadId));
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

Task<HttpResponsePtr> UpdateController::remove(HttpRequestPtr req, std::string installerId)
{
    (void)req;
    HttpResult result;
    const int id = ParseId(installerId);
    if (id <= 0)
    {
        result.setResult(ApiErrorCode::ApiError_InstallerNotFound);
        co_return JsonResponse(result);
    }
    co_return JsonResponse(UpdateService::AdminDeleteInstaller(id));
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
    long long buildNo = 0;
    if (reqJson->isMember("buildNo"))
    {
        const auto &node = (*reqJson)["buildNo"];
        try
        {
            buildNo = node.isInt64() ? node.asInt64()
                     : (node.isString() ? std::stoll(node.asString()) : -1);
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

Task<HttpResponsePtr> UpdateController::publishDelta(HttpRequestPtr req, std::string deltaId)
{
    (void)req;
    HttpResult result;
    const int id = ParseId(deltaId);
    if (id <= 0)
    {
        result.setResult(ApiErrorCode::ApiError_InstallerNotFound);
        co_return JsonResponse(result);
    }
    co_return JsonResponse(UpdateService::AdminPublishDelta(id));
}

Task<HttpResponsePtr> UpdateController::offlineDelta(HttpRequestPtr req, std::string deltaId)
{
    (void)req;
    HttpResult result;
    const int id = ParseId(deltaId);
    if (id <= 0)
    {
        result.setResult(ApiErrorCode::ApiError_InstallerNotFound);
        co_return JsonResponse(result);
    }
    co_return JsonResponse(UpdateService::AdminOfflineDelta(id));
}

Task<HttpResponsePtr> UpdateController::removeDelta(HttpRequestPtr req, std::string deltaId)
{
    (void)req;
    HttpResult result;
    const int id = ParseId(deltaId);
    if (id <= 0)
    {
        result.setResult(ApiErrorCode::ApiError_InstallerNotFound);
        co_return JsonResponse(result);
    }
    co_return JsonResponse(UpdateService::AdminDeleteDelta(id));
}

Task<HttpResponsePtr> UpdateController::listDeltas(HttpRequestPtr req)
{
    (void)req;
    co_return JsonResponse(UpdateService::AdminListDeltas());
}

Task<HttpResponsePtr> UpdateController::deltaJobs(HttpRequestPtr req)
{
    (void)req;
    co_return JsonResponse(DeltaBuilder::AdminListJobs(50));
}

Task<HttpResponsePtr> UpdateController::deltaHealth(HttpRequestPtr req)
{
    (void)req;
    co_return JsonResponse(DeltaBuilder::AdminHealth());
}

Task<HttpResponsePtr> UpdateController::deltaRebuild(HttpRequestPtr req)
{
    HttpResult result;
    auto json = req->getJsonObject();
    if (!json)
    {
        result.setResult(ApiErrorCode::ApiError_InvalidJsonFormat);
        co_return JsonResponse(result);
    }
    const std::string fromVersion = JsonString(*json, "fromVersion");
    const std::string toVersion = JsonString(*json, "toVersion");
    co_return JsonResponse(DeltaBuilder::AdminRebuild(fromVersion, toVersion));
}

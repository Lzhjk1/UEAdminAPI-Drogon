#include "CloudProjectController.h"

#include "services/CloudProjectService.h"
#include "utils/HttpResult.h"

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <filesystem>
#include <fstream>
#include <string>

using namespace drogon;
using namespace UEAdminAPI;
using namespace UEAdminAPI::Services;
using namespace UEAdminAPI::utils;

namespace
{

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

std::string BaseUrl(const HttpRequestPtr &req)
{
    std::string scheme = req->getHeader("x-forwarded-proto");
    if (scheme != "https")
    {
        scheme = "http";
    }
    std::string host = req->getHeader("host");
    if (host.empty())
    {
        host = "127.0.0.1";
    }
    return scheme + "://" + host;
}

long long ParseId(const std::string &text)
{
    try
    {
        return std::stoll(text);
    }
    catch (...)
    {
        return -1;
    }
}

HttpResponsePtr DirectError(HttpStatusCode code, const std::string &message)
{
    Json::Value body;
    body["ok"] = false;
    body["message"] = message;
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(code);
    return resp;
}

} // namespace

Task<HttpResponsePtr> CloudProjectController::status(HttpRequestPtr req)
{
    HttpResult result;
    const int userId = CurrentUserId(req);
    if (userId < 0)
    {
        result.setResult(ApiError_TokenInvalidOrExpired);
        co_return JsonResponse(result);
    }
    co_return JsonResponse(CloudProjectService::Status(userId));
}

Task<HttpResponsePtr> CloudProjectController::prepare(HttpRequestPtr req)
{
    HttpResult result;
    const int userId = CurrentUserId(req);
    if (userId < 0)
    {
        result.setResult(ApiError_TokenInvalidOrExpired);
        co_return JsonResponse(result);
    }
    auto body = req->getJsonObject();
    if (!body)
    {
        result.setResult(ApiError_InvalidJsonFormat);
        co_return JsonResponse(result);
    }
    co_return JsonResponse(CloudProjectService::Prepare(userId, *body, BaseUrl(req)));
}

Task<HttpResponsePtr> CloudProjectController::complete(HttpRequestPtr req)
{
    HttpResult result;
    const int userId = CurrentUserId(req);
    if (userId < 0)
    {
        result.setResult(ApiError_TokenInvalidOrExpired);
        co_return JsonResponse(result);
    }
    auto body = req->getJsonObject();
    if (!body)
    {
        result.setResult(ApiError_InvalidJsonFormat);
        co_return JsonResponse(result);
    }
    co_return JsonResponse(CloudProjectService::Complete(userId, *body));
}

Task<HttpResponsePtr> CloudProjectController::list(HttpRequestPtr req)
{
    HttpResult result;
    const int userId = CurrentUserId(req);
    if (userId < 0)
    {
        result.setResult(ApiError_TokenInvalidOrExpired);
        co_return JsonResponse(result);
    }
    co_return JsonResponse(CloudProjectService::List(userId, req->getParameter("projectName")));
}

Task<HttpResponsePtr> CloudProjectController::download(HttpRequestPtr req)
{
    HttpResult result;
    const int userId = CurrentUserId(req);
    if (userId < 0)
    {
        result.setResult(ApiError_TokenInvalidOrExpired);
        co_return JsonResponse(result);
    }
    const long long fileId = ParseId(req->getParameter("id"));
    if (fileId <= 0)
    {
        result.setResult(ApiError_MissingRequiredArgs, "缺少有效的文件 id");
        co_return JsonResponse(result);
    }
    co_return JsonResponse(CloudProjectService::Download(userId, fileId, BaseUrl(req)));
}

Task<HttpResponsePtr> CloudProjectController::remove(HttpRequestPtr req)
{
    HttpResult result;
    const int userId = CurrentUserId(req);
    if (userId < 0)
    {
        result.setResult(ApiError_TokenInvalidOrExpired);
        co_return JsonResponse(result);
    }
    const long long fileId = ParseId(req->getParameter("id"));
    if (fileId <= 0)
    {
        result.setResult(ApiError_MissingRequiredArgs, "缺少有效的文件 id");
        co_return JsonResponse(result);
    }
    co_return JsonResponse(CloudProjectService::Remove(userId, fileId));
}

void CloudDirectController::upload(const HttpRequestPtr &req,
                                   std::function<void(const HttpResponsePtr &)> &&callback)
{
    const std::string method = req->getParameter("method");
    const std::string key = req->getParameter("key");
    const std::string token = req->getParameter("token");
    const long long exp = ParseId(req->getParameter("exp"));
    if (method != "PUT" || key.empty() || exp <= 0 ||
        !CloudProjectService::VerifyLocalToken(key, "PUT", exp, token))
    {
        callback(DirectError(k403Forbidden, "上传链接无效或已过期"));
        return;
    }

    const auto body = req->body();
    if (body.empty() ||
        static_cast<long long>(body.size()) > CloudProjectService::MaxFileSizeBytes())
    {
        callback(DirectError(k413RequestEntityTooLarge, "文件为空或超过大小限制"));
        return;
    }

    std::string path;
    if (!CloudProjectService::LocalPathFor(key, path))
    {
        callback(DirectError(k500InternalServerError, "云工程存储不可用"));
        return;
    }
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            callback(DirectError(k500InternalServerError, "写入文件失败"));
            return;
        }
        out.write(body.data(), static_cast<std::streamsize>(body.size()));
        out.close();
        if (!out)
        {
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
            callback(DirectError(k500InternalServerError, "写入文件失败"));
            return;
        }
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tmp, path, ec);
    if (ec)
    {
        callback(DirectError(k500InternalServerError, "保存文件失败"));
        return;
    }

    Json::Value ok;
    ok["ok"] = true;
    callback(HttpResponse::newHttpJsonResponse(ok));
}

void CloudDirectController::download(const HttpRequestPtr &req,
                                     std::function<void(const HttpResponsePtr &)> &&callback)
{
    const std::string method = req->getParameter("method");
    const std::string key = req->getParameter("key");
    const std::string token = req->getParameter("token");
    const long long exp = ParseId(req->getParameter("exp"));
    if (method != "GET" || key.empty() || exp <= 0 ||
        !CloudProjectService::VerifyLocalToken(key, "GET", exp, token))
    {
        callback(DirectError(k403Forbidden, "下载链接无效或已过期"));
        return;
    }

    std::string path;
    if (!CloudProjectService::LocalPathFor(key, path) || !std::filesystem::exists(path))
    {
        callback(DirectError(k404NotFound, "云端文件不存在"));
        return;
    }

    auto resp = HttpResponse::newFileResponse(path);
    resp->setContentTypeCode(CT_APPLICATION_OCTET_STREAM);
    callback(resp);
}

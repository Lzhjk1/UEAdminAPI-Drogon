#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

// 厂商团队管理员（含超级管理员）的跨团队管理。
// 只挂 AuthFilter；「是否厂商管理员」由 TeamService::IsVendorOperator 在服务层校验。
class VendorController : public drogon::HttpController<VendorController>
{
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(VendorController::listTeams, "/api/vendor/teams", Get, "AuthFilter");
    ADD_METHOD_TO(VendorController::getTeam, "/api/vendor/teams/{1}", Get, "AuthFilter");
    ADD_METHOD_TO(VendorController::createMember, "/api/vendor/teams/{1}/members", Post, "AuthFilter");
    ADD_METHOD_TO(VendorController::updateMember, "/api/vendor/teams/{1}/members/{2}", Patch, "AuthFilter");
    ADD_METHOD_TO(VendorController::removeMember, "/api/vendor/teams/{1}/members/{2}", Delete, "AuthFilter");
    ADD_METHOD_TO(VendorController::moveMembers, "/api/vendor/teams/{1}/members/move-in", Post, "AuthFilter");
    ADD_METHOD_TO(VendorController::listCandidates, "/api/vendor/users", Get, "AuthFilter");
    METHOD_LIST_END

    Task<HttpResponsePtr> listTeams(HttpRequestPtr req);
    Task<HttpResponsePtr> getTeam(HttpRequestPtr req, std::string teamId);
    Task<HttpResponsePtr> createMember(HttpRequestPtr req, std::string teamId);
    Task<HttpResponsePtr> updateMember(HttpRequestPtr req, std::string teamId, std::string userId);
    Task<HttpResponsePtr> removeMember(HttpRequestPtr req, std::string teamId, std::string userId);
    Task<HttpResponsePtr> moveMembers(HttpRequestPtr req, std::string teamId);
    Task<HttpResponsePtr> listCandidates(HttpRequestPtr req);
};

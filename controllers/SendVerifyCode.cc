#include "SendVerifyCode.h"
#include "utils/HttpResult.h"

// Add definition of your processing function here

using namespace UEAdminAPI;
using namespace UEAdminAPI::utils;

Task<HttpResponsePtr> SendVerifyCode::SendCode(HttpRequestPtr req, std::string target, std::string type) {
    // 依赖
    auto _mfaService = MFAService::Instance();

	auto resp = HttpResponse::newHttpResponse();
    HttpResult result;

    // 检查IP是否处于冷却
    if(IsInColdDown(req->getPeerAddr().toIp())) {    
        result.setResult(ApiErrorCode::ApiError_SendVerifyCodeTooFrequent, "发送过于频繁, 请稍后再试");
        resp->setBody(result.toJsonString());
        co_return resp;
    }

    auto [isSuccess, errMsg] = co_await _mfaService->SendTheCode(target, stringToMFAType(type));

    result.code = isSuccess ? 0 : -1;
    if(!isSuccess) {
        result.msg = errMsg;
    }
    
    resp->setBody(result.toJsonString());
    co_return resp;
}

bool SendVerifyCode::IsInColdDown(std::string ipAddr) {
    std::lock_guard<std::mutex> lock(_mutexForColdDownMap);

    bool isInColdDown = true;

    auto it = _coldDownMap.find(ipAddr);
    // 没找到记录, 就增加这一条记录并返回false, 即没有处于冷却中
    if(it == _coldDownMap.end()) {
        _coldDownMap[ipAddr] = std::chrono::system_clock::now();
        isInColdDown = false;
    }
    // 找到了记录, 检查是否处于冷却中
    else if(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - it->second).count() < _coldDownTime) {
        isInColdDown =  true;
    }
    else{
        _coldDownMap[ipAddr] = std::chrono::system_clock::now();
        isInColdDown =  false;
    }

    // 清除过期的记录
    for(auto it = _coldDownMap.begin(); it != _coldDownMap.end();) {
        if(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - it->second).count() > _coldDownTime) {
            it = _coldDownMap.erase(it);
        }
        else {
            it++;
        }
    }

    return isInColdDown;
}

#include "UserManger.h"
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/open-source-parsers-jsoncpp/traits.h>
#include <jwt-cpp/traits/open-source-parsers-jsoncpp/defaults.h>
#include <drogon/drogon.h>
#include "DataFormatUtils.h"
#include "UserFlashtoken.h"
#include <format>

using namespace drogon_model::UEAdminAPI;
using namespace trantor;

namespace UEAdminAPI {

static std::string jwt_secret;
static std::string jwt_issuer;
static uint64_t userTokenExpeirSec;
static uint64_t userTokenFlashExpeirSec;

struct UserMangerInit {



    UserMangerInit(){
        auto configJson = drogon::app().getCustomConfig();
        jwt_secret = configJson["jwt_secret"].asString();
        jwt_issuer = configJson["jwt_issuer"].asString();
        userTokenExpeirSec = configJson["tokenExpeirSec"].asUInt64();
        userTokenFlashExpeirSec = configJson["tokenFlashTokenSec"].asUInt64();
    }
};

UserMangerInit __userMangerInit;

UserData::UserData(uint64_t id) {
    m_id = id;
}

std::string UserData::CreateToken(uint64_t sec) {
    uint64_t expire_time = time(0) + sec;
    auto jwt_token = jwt::create()
                    .set_issuer(jwt_issuer)
                    .set_expires_at(std::chrono::system_clock::now() + std::chrono::seconds{sec})
                    .set_type("JWS")
                    .set_payload_claim(std::string("id"),jwt::claim(std::to_string(m_id)))
                    .set_payload_claim(std::string("status"),jwt::claim(std::to_string(rand())+std::to_string(rand())))
                    .sign(jwt::algorithm::hs256{jwt_secret});
    LOG_TRACE << "jwt algorithm hs256 secret=" << jwt_secret
                    << " token=" << jwt_token;
    m_tokens.push_back(std::make_pair(jwt_token,expire_time));
    return jwt_token;
}

std::string UserData::CreateFlashToken(uint64_t sec){
    auto jwt_token = jwt::create()
                    .set_issuer(jwt_issuer)
                    .set_expires_at(std::chrono::system_clock::now() + std::chrono::seconds{sec})
                    .set_type("JWS")
                    .set_payload_claim(std::string("id"),jwt::claim(std::to_string(m_id)))
                    .set_payload_claim(std::string("tokenType"),jwt::claim(std::string("flashToken")))
                    .set_payload_claim(std::string("status"),jwt::claim(std::to_string(rand())+std::to_string(rand())))
                    .sign(jwt::algorithm::hs256{jwt_secret});
    LOG_TRACE << "jwt algorithm hs256 secret=" << jwt_secret
                    << " flashtoken=" << jwt_token;
    return jwt_token;
}

bool UserData::checkToken(const std::string& token) {
    ReadLock lock(m_mutex);
    uint64_t now = time(0);
    for(auto i = m_tokens.begin();i!=m_tokens.end();){
        if(i->first == token){
            if(i->second <= now){
                return false;
            }
            return true;
        }
    }
    return false;
}

void UserData::delToken(const std::string& token) {
    WriteLock lock(m_mutex);
    uint64_t now = time(0);
    for(auto it = m_tokens.begin();it!=m_tokens.end();){
        if(it->first == token){
            it = m_tokens.erase(it);
            continue;
        }
        if(it->second <= now){
            it = m_tokens.erase(it);
            continue;
        }
        it++;
    }
}

void UserData::delExpireToken() {
    WriteLock lock(m_mutex);
    uint64_t now = time(0);
    for(auto it = m_tokens.begin();it!=m_tokens.end();){
        if(it->second <= now){
            it = m_tokens.erase(it);
            continue;
        }
        it++;
    }
}

size_t UserData::getTokenSize() {
    return m_tokens.size();
}

UserData::ptr UserDataManager::getUserDataByToken(const std::string& token) {
    if(!verifyToken(token)){
        return nullptr;
    }
    auto decoded_token = jwt::decode(token);
    std::string sid = decoded_token.get_payload_claim("id").as_string();
    int64_t id = std::stoll(sid);
    
    return getUserDataById(id);
    
}

UserData::ptr UserDataManager::getUserDataById(int64_t id) {
    ReadLock lock(m_mutex);
    auto it = m_data.find(id);
    if(it!=m_data.end()){
        return m_data[id];
    }
    return nullptr;
}

const std::string UserDataManager::CreateToken(int64_t id) {
    UserData::ptr user_data;
    user_data = getUserDataById(id);
    if(user_data){
        return user_data->CreateToken(userTokenExpeirSec);
    }else{
        WriteLock lock(m_mutex);
        auto it = m_data.find(id);
        if(it!=m_data.end()){
            user_data = it->second;
        }else{
            user_data.reset(new UserData(id));
            m_data[id] = user_data;
        }
    }
    return user_data->CreateToken(userTokenExpeirSec);
    
}

std::string  UserDataManager::CreateFlashToken(int64_t id){

    UserData::ptr user_data;
    user_data = getUserDataById(id);
    if(user_data){
        return user_data->CreateFlashToken(userTokenFlashExpeirSec);
    }else{
        WriteLock lock(m_mutex);
        auto it = m_data.find(id);
        if(it!=m_data.end()){
            user_data = it->second;
        }else{
            user_data.reset(new UserData(id));
            m_data[id] = user_data;
        }
    }
    return user_data->CreateFlashToken(userTokenFlashExpeirSec);
}

std::string UserDataManager::getFlashToken(int64_t id){
    std::string flashToken;

    auto dbClientPtr = drogon::app().getDbClient();
    drogon::orm::Mapper<UserFlashtoken> mapper(dbClientPtr);

    UserFlashtoken flashTokenRow;
    bool isFoundTarget = true;

    // 在数据库查找FlashToken
    try {
        flashTokenRow = mapper.findByPrimaryKey(id);
    }
    // 查找时如果没有数据会抛出异常
    catch (const drogon::orm::UnexpectedRows& e) {
        LOG_DEBUG << std::format("按照id查找flashToken时, 没找到数据, id = {}, 将创建新的flashToken", id);
        isFoundTarget = false;
    }
    std::string flashtoken = flashTokenRow.getValueOfFlashToken();

    int64_t temp_id;
    bool needNewOne = !isFoundTarget || !UserDataManager::verifyFlashToken(flashtoken,temp_id);
    auto now = trantor::Date::now();
    Date expire_time;

    if(flashTokenRow.getValueOfExpireAt() < now || needNewOne){
        // 如果过期或token无效, 则生成新的token
        flashToken = UserDataMgr::GetInstance()->CreateFlashToken(id);
        expire_time = now.after(UserDataMgr::GetInstance()->getFlashTokenExpireSec());
    }else{
        flashToken = flashTokenRow.getValueOfFlashToken();
        expire_time = flashTokenRow.getValueOfExpireAt();
    }

    if(!isFoundTarget){
        // 如果没找到数据, 则插入新的数据
        UserFlashtoken newFlashTokenRow;
        newFlashTokenRow.setUserId(id);
        newFlashTokenRow.setFlashToken(flashToken);
        newFlashTokenRow.setExpireAt(trantor::Date(expire_time));
        newFlashTokenRow.setTokenDesc("");

        try {
            mapper.insert(newFlashTokenRow);
        }
        catch (const std::exception& e){
            LOG_DEBUG << std::format("插入新的flashToken时出错, id = {}, flashToken = {}, expire_time = {}, 错误信息: {}", id, flashToken, expire_time.toFormattedString(false), e.what());
            return "";
        }
    }
    else if(flashTokenRow.getValueOfExpireAt() < now || needNewOne){
        // 如果过期则更新数据库中的数据
        flashTokenRow.setFlashToken(flashToken);
        flashTokenRow.setExpireAt(expire_time);
        
        try {
            mapper.update(flashTokenRow);
        }
        catch (const std::exception& e) {
            LOG_DEBUG << std::format("更新flashToken时出错, id = {}, flashToken = {}, expire_time = {}, 错误信息: {}", id, flashToken, expire_time.toFormattedString(false), e.what());
            return "";
        }
    }
    return flashToken;
}

int64_t UserDataManager::getUserIdByToken(const std::string& token) {
    if(!verifyToken(token)){
        return -1;
    }
    auto decoded_token = jwt::decode(token);
    std::string sid = decoded_token.get_payload_claim("id").as_string();
    int64_t id = std::stoll(sid);
    UserData::ptr user_data = getUserDataById(id);
    if(user_data->checkToken(token)){
        return id;
    }
    return -1;
}

bool UserDataManager::verifyToken(const std::string& token){
    if(!DataFormatUtil::isJwtString(token)){
        return false;
    }
    auto verifier = jwt::verify()
    .allow_algorithm(jwt::algorithm::hs256{ jwt_secret })
    .with_issuer(jwt_issuer);
    auto decoded_token = jwt::decode(token);
    try{
        verifier.verify(decoded_token);
        return true;
    }catch(...){
        return false;
    } 
}

bool UserDataManager::checkToken(const std::string& token) {
    if(!verifyToken(token)){
        return false;
    }
    auto decoded_token = jwt::decode(token);
    std::string sid = decoded_token.get_payload_claim("id").as_string();
    if(sid.empty()){
        return false;
    }
    int64_t id = std::stoll(sid);
    {
        ReadLock lock(m_mutex);
        auto it = m_data.find(id);
        if(it!=m_data.end()){
            return it->second->checkToken(token);
        }
    }
    return false;
}

void UserDataManager::deleteTokenById(int64_t id){
    WriteLock lock(m_mutex);
    m_data.erase(id);
}

uint64_t UserDataManager::getTokenExpireSec() {
    return userTokenExpeirSec;
}

uint64_t UserDataManager::getFlashTokenExpireSec() {
    return userTokenFlashExpeirSec;
}

bool UserDataManager::verifyFlashToken(const std::string& flashToken,int64_t& id){
    if(!verifyToken(flashToken)){
        return false;
    }
    auto decoded_token = jwt::decode(flashToken);
    std::string sid = decoded_token.get_payload_claim("id").as_string();
    if(sid.empty()){
        return false;
    }
    id = std::stoll(sid);
    return true;
}

void UserDataManager::clearExipre(){
    {
        WriteLock lock(m_mutex);
        for( auto it = m_data.begin(); it != m_data.end();){
            it->second->delExpireToken();
            if(it->second->getTokenSize() == 0){
                it = m_data.erase(it);
            }else{
                it++;
            }

        }
    }
    


}

}


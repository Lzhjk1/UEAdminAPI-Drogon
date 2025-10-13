#include "user_manger.h"
#include "qtch/log.h"
#include <jwt-cpp/jwt.h>
#include "qtch/config.h"
#include "qtch/util.h"
#include "orm_out/uesoft/im/im_user_flashtoken_info.h"
#include "qtch/db/postgresql.h"
#include "UEIMHttpServer/util.h"


namespace uehttp {

qtch::ConfigVar<std::string>::ptr s_jwt_secret = qtch::Config::LookUp("uehttp.server.jwt.secret",std::string("1dqwh8923fdao0v4"),"jwt secret");
qtch::ConfigVar<std::string>::ptr s_jwt_issuer = qtch::Config::LookUp("uehttp.server.jwt.issuer",std::string("uesoft"),"jwt issuer");
qtch::ConfigVar<uint64_t>::ptr s_userToken_expeir_sec = qtch::Config::LookUp("uehttp.server.usertoken.experir_sec",(uint64_t)2*60*60,"userToken experir time(second)");
qtch::ConfigVar<uint64_t>::ptr s_userFlashToken_expeir_sec = qtch::Config::LookUp("uehttp.server.userflashtoken.experir_sec",(uint64_t)2*30*24*60*60,"userFlashToken experir time(second)");
static std::string jwt_secret;
static std::string jwt_issuer;
static uint64_t userTokenExpeirSec;
static uint64_t userTokenFlashExpeirSec;

struct UserMangerInit {



    UserMangerInit(){
        QTCH_CONFIG_VALUE_ADD_LISTENER_STRING(s_jwt_secret,jwt_secret);
        QTCH_CONFIG_VALUE_ADD_LISTENER_STRING(s_jwt_issuer,jwt_issuer);
        QTCH_CONFIG_VALUE_ADD_LISTENER_UINT64(s_userToken_expeir_sec,userTokenExpeirSec);
        QTCH_CONFIG_VALUE_ADD_LISTENER_UINT64(s_userFlashToken_expeir_sec,userTokenFlashExpeirSec);
    }
};

UserMangerInit __userMangerInit;

static qtch::Logger::ptr logger = QTCH_LOG_NAME("uehttp"); 

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
    QTCH_LOG_DEBUG(logger) << "jwt algorithm hs256 secret=" << jwt_secret
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
    QTCH_LOG_DEBUG(logger) << "jwt algorithm hs256 secret=" << jwt_secret
                    << " flashtoken=" << jwt_token;
    return jwt_token;
}

bool UserData::checkToken(const std::string& token) {
    RWMutexType::ReadLock lock(m_mutex);
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
    RWMutexType::WriteLock lock(m_mutex);
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
    RWMutexType::WriteLock lock(m_mutex);
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

UserData::ptr UserDataManger::getUserDataByToken(const std::string& token) {
    if(!verifyToken(token)){
        return nullptr;
    }
    auto decoded_token = jwt::decode(token);
    std::string sid = decoded_token.get_payload_claim("id").as_string();
    int64_t id = qtch::TypeUtil::Atoi(sid);
    
    return getUserDataById(id);
    
}

UserData::ptr UserDataManger::getUserDataById(int64_t id) {
    RWMutexType::ReadLock lock(m_mutex);
    auto it = m_data.find(id);
    if(it!=m_data.end()){
        return m_data[id];
    }
    return nullptr;
}

const std::string UserDataManger::CreateToken(int64_t id) {
    UserData::ptr user_data;
    user_data = getUserDataById(id);
    if(user_data){
        return user_data->CreateToken(userTokenExpeirSec);
    }else{
        RWMutexType::WriteLock lock(m_mutex);
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

std::string  UserDataManger::CreateFlashToken(int64_t id){

    UserData::ptr user_data;
    user_data = getUserDataById(id);
    if(user_data){
        return user_data->CreateFlashToken(userTokenFlashExpeirSec);
    }else{
        RWMutexType::WriteLock lock(m_mutex);
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

std::string UserDataManger::getFlashToken(int64_t id){
    std::string flashToken;
    qtch::PostgreSQL::ptr pq_ptr = qtch::PostgresqlMgr::GetInstance()->get("aliyun");
    uesoft::im::ImUserFlashtokenInfo::ptr flashTokenPtr = uesoft::im::ImUserFlashtokenInfoDao::SelectByid(id,pq_ptr);
    int64_t temp_id;
    bool needNewOne = !flashTokenPtr || !UserDataManger::verifyFlashToken(flashTokenPtr->getFlashToken(),temp_id);
    int64_t now = time(0);
    int64_t expire_time;
    if(!flashTokenPtr||flashTokenPtr->getExpireTime()<now || needNewOne){
        flashToken = UserDataMgr::GetInstance()->CreateFlashToken(id);
        expire_time = UserDataMgr::GetInstance()->getFlashTokenExpireSec() + now;
    }else{
        flashToken = flashTokenPtr->getFlashToken();
        expire_time = flashTokenPtr->getExpireTime();
    }

    if(!flashTokenPtr){
        flashTokenPtr.reset(new uesoft::im::ImUserFlashtokenInfo);
        flashTokenPtr->setId(id);
        flashTokenPtr->setFlashToken(flashToken);
        flashTokenPtr->setExpireTime(expire_time);
        if(!uesoft::im::ImUserFlashtokenInfoDao::Insert(flashTokenPtr,pq_ptr)){
            QTCH_LOG_ERROR(logger) << "ImUserFlashtokenInfoDao::Insert id=" << flashTokenPtr->getId()
                        << " flashToken=" << flashTokenPtr->getFlashToken()
                        << " expireTime=" << flashTokenPtr->getExpireTime()
                        << " errno=" << pq_ptr->getError()
                        << " error=" << pq_ptr->getErrStr();
            return "";
        }
    }else if(flashTokenPtr->getExpireTime()<now || needNewOne){
        flashTokenPtr->setFlashToken(flashToken);
        flashTokenPtr->setExpireTime(expire_time);
        if(!uesoft::im::ImUserFlashtokenInfoDao::Update(flashTokenPtr,pq_ptr)){
            QTCH_LOG_ERROR(logger) << "ImUserFlashtokenInfoDao::Update id=" << flashTokenPtr->getId()
                        << " flashToken=" << flashTokenPtr->getFlashToken()
                        << " expireTime=" << flashTokenPtr->getExpireTime()
                        << " errno=" << pq_ptr->getError()
                        << " error=" << pq_ptr->getErrStr();
            return "";
        }
    }
    return flashToken;
}

int64_t UserDataManger::getUserIdByToken(const std::string& token) {
    if(!verifyToken(token)){
        return -1;
    }
    auto decoded_token = jwt::decode(token);
    std::string sid = decoded_token.get_payload_claim("id").as_string();
    int64_t id = qtch::TypeUtil::Atoi(sid);
    UserData::ptr user_data = getUserDataById(id);
    if(user_data->checkToken(token)){
        return id;
    }
    return -1;
}

bool UserDataManger::verifyToken(const std::string& token){
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

bool UserDataManger::checkToken(const std::string& token) {
    if(!verifyToken(token)){
        return false;
    }
    auto decoded_token = jwt::decode(token);
    std::string sid = decoded_token.get_payload_claim("id").as_string();
    if(sid.empty()){
        return false;
    }
    int64_t id = qtch::TypeUtil::Atoi(sid);
    {
        RWMutexType::ReadLock lock(m_mutex);
        auto it = m_data.find(id);
        if(it!=m_data.end()){
            return it->second->checkToken(token);
        }
    }
    return false;
}

void UserDataManger::deleteTokenById(int64_t id){
    RWMutexType::WriteLock lock(m_mutex);
    m_data.erase(id);
}

uint64_t UserDataManger::getTokenExpireSec() {
    return userTokenExpeirSec;
}

uint64_t UserDataManger::getFlashTokenExpireSec() {
    return userTokenFlashExpeirSec;
}

bool UserDataManger::verifyFlashToken(const std::string& flashToken,int64_t& id){
    if(!verifyToken(flashToken)){
        return false;
    }
    auto decoded_token = jwt::decode(flashToken);
    std::string sid = decoded_token.get_payload_claim("id").as_string();
    if(sid.empty()){
        return false;
    }
    id = qtch::TypeUtil::Atoi(sid);
    return true;
}

void UserDataManger::clearExipre(){
    {
        RWMutexType::WriteLock lock(m_mutex);
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


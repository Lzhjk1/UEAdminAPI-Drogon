#ifndef __UEHTTP_USER_MANGER_H__
#define __UEHTTP_USER_MANGER_H__

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include "Singleton.h"

namespace UEAdminAPI {

class UserData{
public:
    typedef std::shared_ptr<UserData> ptr;
    typedef std::shared_mutex RWMutexType;
    typedef std::unique_lock<std::shared_mutex> WriteLock;
    typedef std::shared_lock<std::shared_mutex> ReadLock;

    UserData(uint64_t id);
    std::string CreateToken(uint64_t sec);
    std::string CreateFlashToken(uint64_t sec);
    bool checkToken(const std::string& token);
    void delToken(const std::string& token);
    void delExpireToken();
    size_t getTokenSize();


    int64_t getId() const {return m_id;}
private:
    RWMutexType m_mutex;
    int64_t m_id;
    // token和token的过期时间
    std::vector<std::pair<std::string,uint64_t> > m_tokens;
    
};

class UserDataManager {
public:
    typedef std::shared_ptr<UserDataManager> ptr;
    typedef std::shared_mutex RWMutexType;
    typedef std::unique_lock<std::shared_mutex> WriteLock;
    typedef std::shared_lock<std::shared_mutex> ReadLock;

    UserData::ptr getUserDataByToken(const std::string& token);
    UserData::ptr getUserDataById(int64_t id);
    const std::string CreateToken(int64_t id);
    std::string CreateFlashToken(int64_t id);
    std::string getFlashToken(int64_t id);
    int64_t getUserIdByToken(const std::string& token);
    bool checkToken(const std::string& token);

    void deleteTokenById(int64_t id);

    uint64_t getTokenExpireSec();
    uint64_t getFlashTokenExpireSec();

    static bool verifyFlashToken(const std::string& flashToken,int64_t& id);

    void clearExipre();

protected:
    static bool verifyToken(const std::string& token); 

private:
    RWMutexType m_mutex;
    std::unordered_map<int64_t,UserData::ptr> m_data;

};

using UserDataMgr = Singleton<UserDataManager>;



}


#endif
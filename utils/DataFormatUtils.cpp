#include "DataFormatUtils.h"
#include <regex>

namespace UEAdminAPI {


bool DataFormatUtil::isPhoneNumber(const std::string& phoneNumber) {
    static const std::regex pattern(
        "^(13[0-9]|14[01456879]|15[0-35-9]|16[2567]|17[0-8]|18[0-9]|19[0-35-9])\\d{8}$");
    return std::regex_match(phoneNumber, pattern);
}

bool DataFormatUtil::isEmail(const std::string& email) {
    static const std::regex pattern(
        "^\\w+([-+.]\\w+)*@\\w+([-.]\\w+)*\\.\\w+([-.]\\w+)*$");
    return std::regex_match(email, pattern);
}

bool DataFormatUtil::isJwtString(const std::string& jwt) {
    static const std::regex pattern(
        "ey[A-Za-z0-9_-]*\\.[A-Za-z0-9._-]*");
    return std::regex_match(jwt, pattern);
}


std::string DataFormatUtil::encryptPwd(const std::string& userName,const std::string& userPwd){
    // std::stringstream ss;
    // ss << userName << "|" << "uesoft" << "|" << userPwd;
    // std::string encrypt_pwd = qtch::HashUtil::md5Sum(ss.str());
    // return encrypt_pwd;
    throw std::runtime_error("not implement");
}

std::string DataFormatUtil::ParseSex(int sex){
    switch (sex)
    {
    case 0:
        return "woman";
        break;
    case 1:
        return "man";
        break;
    case 2:
        return "unset";
        break;
    default:
        return "invalid";
        break;
    }
}

std::string DataFormatUtil::RandomCode(int len){
    if(len<=0){
        return "";
    }
    int num = rand();
    std::string result;
    result.resize(len);
    for(int i=0;i<len;++i){
        result[i] = (num%10) + '0';
        num/=10;
        while(num<=0){
            num = rand();
        }
    }
    return result;
}

std::string DataFormatUtil::RandomCode(const std::string& source,int len){
    if(len<=0){
        return "";
    }
    int s_len = source.length();
    std::string result;
    result.resize(len);
    for(int i = 0; i < len; ++i){
        int num = rand() % s_len;
        result[i] = source[num];
    }
    return result;
}

    
} // namespace uehttp

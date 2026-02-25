#include "DataFormatUtils.h"
#include <regex>
#include <cctype>

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

std::string DataFormatUtil::toLowerCase(const std::string &str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

bool DataFormatUtil::checkUserName(const std::string &userName) {
    static const std::regex pattern("^[a-zA-Z][a-zA-Z0-9_]{5,19}$");
    return std::regex_match(userName, pattern);
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

static std::string _trim(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
    size_t j = s.size();
    while (j > i && std::isspace(static_cast<unsigned char>(s[j - 1]))) j--;
    return s.substr(i, j - i);
}

std::tuple<AuthorizationTokenType, std::string> DataFormatUtil::parseTokenFromAuthorizationHeader(const std::string& authHeader) {
    if (authHeader.empty()) return {AuthorizationTokenType::Unknown, ""};
    std::string s = _trim(authHeader);
    auto pos = s.find(' ');
    std::string scheme = pos != std::string::npos ? s.substr(0, pos) : "";
    std::string token = pos != std::string::npos ? s.substr(pos + 1) : s;
    token = _trim(token);
    std::string lower = toLowerCase(scheme);
    AuthorizationTokenType type = AuthorizationTokenType::Unknown;
    if (lower == "bearer") type = AuthorizationTokenType::Bearer;
    else if (!lower.empty()) type = AuthorizationTokenType::Other;
    return {type, token};
}

std::string DataFormatUtil::trim(const std::string& s) {
    if (s.empty()) return s;

    // ������Ҫ���˵Ŀհ��ַ���ASCII ��Χ����ȫ����ȷ��
    const std::string whitespace = " \t\n\r\f\v";

    // Ѱ�ҵ�һ���ǿհ��ַ�
    size_t start = s.find_first_not_of(whitespace);
    if (start == std::string::npos) return ""; // ȫ�ǿո�����

    // Ѱ�����һ���ǿհ��ַ�
    size_t end = s.find_last_not_of(whitespace);

    return s.substr(start, end - start + 1);
}
 
    
} // namespace uehttp

#pragma once

#include <string>
#include <tuple>
#include <algorithm>

namespace UEAdminAPI {

    enum class AuthorizationTokenType {
        Unknown,
        Bearer,
        Other
    };

    class DataFormatUtil {
    public:
        /**
         * @brief using regular expression to check is phoneNumber
         *
         * @param phoneNumber
         * @return true
         * @return false
         */
        static bool isPhoneNumber(const std::string &phoneNumber);

        /**
         * @brief using regular expression to check is email
         *
         * @param email
         * @return true
         * @return false
         */
        static bool isEmail(const std::string &email);

        /**
         * @brief using md5 encrypt pwd
         *
         * @param userName
         * @param userPwd
         * @return std::string
         */
        static std::string encryptPwd(const std::string &userName, const std::string &userPwd);

        /**
         * @brief parse sex, 0 for man, 1 for woman
         *
         * @param sex
         * @return std::string
         */
        static std::string ParseSex(int sex);

        /**
         * @brief random a digit code
         *
         * @param len code len
         * @return std::string
         */
        static std::string RandomCode(int len);

        /**
         * @brief 根据source内容随机一个字符串
         *
         * @param source 字符来源
         * @param len 随机字符串长度
         * @return std::string
         */
        static std::string RandomCode(const std::string &source, int len);

        /**
         * @brief 判断字符串是否是jwt字符串
         *
         * @param jwt
         * @return true 是jwt字符串
         * @return false 不是jwt字符串
         */
        static bool isJwtString(const std::string &jwt);

        // 转换字符串为小写
        static std::string toLowerCase(const std::string &str);

        /**
         * 检查用户名格式是否有效, 只允许字母开头, 长度在6-20之间, 允许字母数字下划线
         * @param userName 待检查的用户名字符串
         * @return 如果用户名格式符合要求返回true，否则返回false
         */
        static bool checkUserName(const std::string &userName);
        static std::tuple<AuthorizationTokenType, std::string> parseTokenFromAuthorizationHeader(const std::string &authHeader);

        static std::string trim(const std::string& s);
    };

}

#pragma once

#include <iostream>
#include <json/json.h>
#include <utility>  // for std::pair
#include <vector>

// // 基本类型的序列化特化
// template<typename T>
// inline Json::Value SerializeData(const T& data) {
//     Json::Value value;
//     value = data;  // 对于基本类型，直接赋值即可
//     return value;
// }

// // std::string 的特化
// template<>
// inline Json::Value SerializeData<std::string>(const std::string& data) {
//     return Json::Value(data);
// }

// // std::pair 的特化
// template<typename T, typename U>
// inline Json::Value SerializeData(const std::pair<T, U>& data) {
//     // 序列化为数组
//     Json::Value value(Json::arrayValue);
//     value.append(SerializeData(data.first));
//     value.append(SerializeData(data.second));
//     return value;
// }

// // vector 的特化
// template<typename T>
// inline Json::Value SerializeData(const std::vector<T>& data) {
//     Json::Value value(Json::arrayValue);
//     for (const auto& item : data) {
//         value.append(SerializeData(item));
//     }
//     return value;
// }

// // std::chrono::system_clock::time_point 的特化
// template<>
// inline Json::Value SerializeData<std::chrono::system_clock::time_point>(const std::chrono::system_clock::time_point& data) {
//     // 转换为时间戳（秒）
//     auto duration = data.time_since_epoch();
//     auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
//     return Json::Value(static_cast<int64_t>(timestamp));
// }

template<typename T>
class ServiceResponse {
public:
    T Data;
    bool Success;
    std::string Message;

public:
    ServiceResponse(T data, bool success, std::string message) {
        Data = data;
        Success = success;
        Message = message;
    }
    ServiceResponse(){
        Data = T();
        Success = false;
        Message = "";
    }

    Json::Value ToJson() {
        Json::Value root;
        root["success"] = Success;
        root["message"] = Message;
        return root;
    }
};

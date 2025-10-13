#include <drogon/drogon.h>
#include "controllers/TestController.h"

#ifdef _WIN32
#include <windows.h>
#include "utils/AuthRepository.h"

// 设置控制台为UTF-8编码
void SetConsoleUTF8() {
	// 获取控制台输出句柄
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	// 获取当前控制台模式
	DWORD dwMode = 0;
	GetConsoleMode(hOut, &dwMode);
	// 启用UTF-8编码
	SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

	// 设置代码页为UTF-8
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
}
#endif

int main() {
     #ifdef _WIN32
     SetConsoleUTF8();
     #endif

    HttpAppFramework &app = drogon::app();
    //Set HTTP listener address and port
    app.addListener("0.0.0.0", 5555);
    //Load config file
    //drogon::app().loadConfigFile("../config.json");
    app.loadConfigFile("../config.yaml");
    //Run HTTP framework,the method will block in the internal event loop
    auto config = app.getCustomConfig();
    
    //// 1. 先判断是否为JSON对象
    //if (config.isObject()) {
    //    // 2. 获取所有键（返回std::vector<std::string>）
    //    std::vector<std::string> keys = config.getMemberNames();

    //    // 3. 遍历键，获取对应的值
    //    for (const std::string& key : keys) {
    //        // 通过键获取值
    //        Json::Value value = config[key];

    //        // 打印键和值（根据值的类型做不同处理）
    //        std::cout << "键: " << key << "，值: ";

    //        if (value.isString()) {
    //            std::cout << value.asString();
    //        }
    //        else if (value.isInt()) {
    //            std::cout << value.asInt();
    //        }
    //        else if (value.isBool()) {
    //            std::cout << (value.asBool() ? "true" : "false");
    //        }
    //        // 其他类型（如double、数组等）可类似处理
    //        std::cout << std::endl;
    //    }
    //}
    std::string secert = config["secert"].asString();
    if (secert.empty()) {
        //std::cout << "secert为空, 请先前往配置文件设置" << std::endl;
        LOG_FATAL << "secert为空, 请先前往配置文件设置";
        return -1;
    }
    UEAdminAPI::AuthRepository::SECRET = secert;
    
    app.run();
    return 0;
}

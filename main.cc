#include <drogon/drogon.h>
#include <filesystem>
#include <fstream>
#include <json/json.h>

#include "plugins/SMTPMail.h"
#include <trantor/utils/Logger.h>

// 需要初始化的单例服务
#include "services/TencentSMSService.h"
#include "services/MFAService.h"
#include "services/EmailService.h"
#include "services/AuthService.h"
#include "services/GitlabService.h"
#include "services/ThirdPartyLoginService.h"
#include "services/SystemService.h"
#include "services/ActionTokenService.h"

#include "controllers/OAuth2Controller.h"

#ifdef _WIN32
#include <windows.h>

#include <iostream>

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
    
    // 查找config.yaml文件（最多向上一级找三层）
    std::string config_path = "";
    std::filesystem::path current_path = std::filesystem::current_path();
    for (int i = 0; i <= 3; ++i) {
        std::filesystem::path file_path = current_path / "config.yaml";
        if (std::filesystem::exists(file_path)) {
            config_path = file_path.string();
            break;
        }
        if (!current_path.has_parent_path()) {
            break;
        }
        current_path = current_path.parent_path();
    }
    
    if (config_path.empty()) {
        std::cerr << "错误: 无法找到配置文件 config.yaml (在当前目录及向上三层级内)" << std::endl;
        return 1;
    }
    
    // 解析配置文件，检查并创建日志目录
    try {
        std::ifstream configFile(config_path);
        if (configFile.is_open()) {
            std::string content((std::istreambuf_iterator<char>(configFile)),
                                 std::istreambuf_iterator<char>());
            Json::Value config;
            Json::CharReaderBuilder builder;
            std::string errs;
            // 使用 yaml 插件解析（因为文件是 .yaml）
            if (config_path.find(".yaml") != std::string::npos || config_path.find(".yml") != std::string::npos) {
                // 如果是 yaml，手动解析比较复杂，我们改用一种简单的方式：
                // 读取整个文本，用正则或简单的字符串查找找 "log_path:"
                std::istringstream iss(content);
                std::string line;
                while (std::getline(iss, line)) {
                    // 去除行首空白
                    size_t first = line.find_first_not_of(" \t");
                    if (first != std::string::npos) {
                        line = line.substr(first);
                    }
                    if (line.find("log_path:") == 0) {
                        size_t colon_pos = line.find(':');
                        if (colon_pos != std::string::npos) {
                            std::string logPath = line.substr(colon_pos + 1);
                            // 去除空白和可能的引号
                            logPath.erase(0, logPath.find_first_not_of(" \t\"'"));
                            logPath.erase(logPath.find_last_not_of(" \t\"'\r") + 1);
                            
                            if (!logPath.empty() && logPath != "''" && logPath != "\"\"" && !std::filesystem::exists(logPath)) {
                                std::filesystem::create_directories(logPath);
                            }
                        }
                        break; // 找到后退出循环
                    }
                }
            } else {
                // 如果是 json 文件
                std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
                if (reader->parse(content.c_str(), content.c_str() + content.length(), &config, &errs)) {
                    if (config.isMember("app") && config["app"].isMember("log") && config["app"]["log"].isMember("log_path")) {
                        std::string logPath = config["app"]["log"]["log_path"].asString();
                        if (!logPath.empty() && !std::filesystem::exists(logPath)) {
                            std::filesystem::create_directories(logPath);
                        }
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "检查日志目录时出错: " << e.what() << std::endl;
    }
    
    app.loadConfigFile(config_path);
    
    // app 启动后回调中初始化单例服务（消除竞态条件）
    app.registerBeginningAdvice([]{
        LOG_INFO << "启动成功, 开始初始化插件与服务";
        // 初始化单例服务
        auto smtpPlugin = drogon::app().getPlugin<SMTPMail>();
        AuthService::Init(drogon::app().getCustomConfig());
        TencentSMSService::Init(drogon::app().getCustomConfig());
        EmailService::Init(smtpPlugin, drogon::app().getCustomConfig());
        MFAService::Init(
            EmailService::Instance(), 
            TencentSMSService::Instance()
        );
        UEAdminAPI::GitlabService::Init(drogon::app().getCustomConfig());
        UEAdminAPI::Services::ThirdPartyLoginService::Init(drogon::app().getCustomConfig());
        SystemService::Init();
        UEAdminAPI::Services::ActionTokenService::Init(drogon::app().getCustomConfig());

        LOG_INFO << "服务初始化完成";
    });

    LOG_DEBUG << "即将启动...";

    //Run HTTP framework,the method will block in the internal event loop
    app.run();
    return 0;
}

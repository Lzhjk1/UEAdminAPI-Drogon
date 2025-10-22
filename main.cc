#include <drogon/drogon.h>
#include "controllers/TestController.h"

#ifdef _WIN32
#include <windows.h>
#include "utils/AuthRepository.h"
#include "plugins/SMTPMail.h"
#include "services/TencentSMSService.h"

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
    app.loadConfigFile("../../../config.yaml");

    TencentSMSService::Init(app.getCustomConfig());

    LOG_DEBUG << "即将启动...";

    //Run HTTP framework,the method will block in the internal event loop
    app.run();
    return 0;
}

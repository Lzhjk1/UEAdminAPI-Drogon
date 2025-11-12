#include <drogon/drogon.h>
#include "controllers/TestController.h"

#ifdef _WIN32
#include <windows.h>
#include "plugins/SMTPMail.h"

// 需要初始化的单例服务
#include "services/TencentSMSService.h"
#include "services/MFAService.h"
#include "services/EmailService.h"
#include "services/AuthService.h"
#include "services/GitlabService.h"
#include "services/ThirdPartyLoginService.h"


#include "utils/ServiceResponse.h"

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


    
    // 因为插件需要启动后才能获取, 所以启动后再初始化
    auto loop = app.getLoop();
    loop->runAfter(1 , [&app]{
        if (!app.isRunning()) {
            LOG_INFO << "未启动, 等待1秒后重试";
            sleepCoro(app.getLoop(), 1);
        }
        LOG_INFO << "启动成功, 开始初始化插件与服务";
        // 初始化单例服务
        auto smtpPlugin = app.getPlugin<SMTPMail>();
        AuthService::Init(app.getCustomConfig());
        TencentSMSService::Init(app.getCustomConfig());
        EmailService::Init(smtpPlugin, app.getCustomConfig());
        MFAService::Init(
            EmailService::Instance(), 
            TencentSMSService::Instance()
        );
        UEAdminAPI::GitlabService::Init(app.getCustomConfig());
        UEAdminAPI::Services::ThirdPartyLoginService::Init(app.getCustomConfig());

        LOG_INFO << "服务初始化完成";
    });

    LOG_DEBUG << "即将启动...";

    //Run HTTP framework,the method will block in the internal event loop
    app.run();
    return 0;
}

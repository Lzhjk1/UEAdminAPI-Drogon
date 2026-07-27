#pragma once

#include <string>
#include <json/json.h>
#include <trantor/utils/Logger.h>

/**
 * @brief 测试模式配置持有类
 *
 * 自动化测试专用。生产环境务必保持 config 中 TestMode.Enable = false。
 * 开启后: 验证码不再真实发送(短信/邮件), 改为固定值或随机值并直接入库,
 *         以便测试侧通过调试接口 GET /api/test/mfa/latest 查询验证码,
 *         实现无人值守的端到端自动化测试。
 *
 * 读取 custom_config["TestMode"]:
 *   - Enable       (bool)  总开关
 *   - FixedCode    (string)固定验证码, 留空则保持随机生成
 *   - ColdDownSec  (int)   测试模式下的发送冷却时间(秒), 0 表示不限制
 */
class TestModeConfig {
public:
    static void Init(const Json::Value &customConfig) {
        if (!customConfig.isMember("TestMode")) {
            _enable = false;
            _fixedCode.clear();
            _coldDownSec = 0;
            return;
        }
        const auto &tm = customConfig["TestMode"];
        _enable = tm.get("Enable", false).asBool();
        _fixedCode = tm.get("FixedCode", "").asString();
        _coldDownSec = tm.get("ColdDownSec", 0).asInt();
        if (_enable) {
            LOG_WARN << "============================================================";
            LOG_WARN << "[TestMode] 测试模式已开启! 验证码将不会真实发送!";
            LOG_WARN << "[TestMode] 固定验证码: "
                     << (_fixedCode.empty() ? std::string("<随机>") : _fixedCode)
                     << ", 冷却时间: " << _coldDownSec << " 秒";
            LOG_WARN << "[TestMode] 生产环境请务必保持 TestMode.Enable = false";
            LOG_WARN << "============================================================";
        }
    }

    static bool Enable() { return _enable; }
    static const std::string &FixedCode() { return _fixedCode; }
    static int ColdDownSec() { return _coldDownSec; }

private:
    inline static bool _enable = false;
    inline static std::string _fixedCode;
    inline static int _coldDownSec = 0;
};
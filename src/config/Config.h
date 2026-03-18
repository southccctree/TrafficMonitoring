#pragma once

// ============================================================
//  Config.h  v2
//  新增：MonitorConfig.vpnInterface — VPN 网卡名配置项
//        AlertConfig.notifyCooldownSec — 弹窗冷却时间
// ============================================================

#include <string>
#include <cstdint>

namespace NetGuard {

struct WindowConfig {
    int positionX = 20;
    int positionY = 20;
    int opacity   = 200;
    int width     = 260;
    int height    = 160;
};

struct AlertConfig {
    int    dailyLimitMB            = 1024;
    int    vpnLimitMB              = 5120;
    int    alertThresholdPercent   = 80;
    int    warnThresholdPercent    = 95;
    double speedAlertUploadKbps   = 1024.0;
    double speedAlertDownloadKbps = 2048.0;
    int    notifyCooldownSec      = 600;   // 弹窗冷却时间（秒），默认 10 分钟
};

struct MonitorConfig {
    int         refreshIntervalMs = 1000;
    std::string networkInterface  = "auto"; // 主网卡（公网流量）
    std::string vpnInterface      = "";     // VPN 网卡，空字符串表示不单独统计
    std::string clashConfigPath   = "";          // Clash config.yaml 路径
    std::string clashApiHost      = "127.0.0.1"; // Clash API 主机
    uint16_t    clashApiPort      = 60082;         // Clash API 端口
    std::string clashApiSecret    = "";          // Clash API Bearer 密钥
};

struct AppConfig {
    WindowConfig  window;
    AlertConfig   alert;
    MonitorConfig monitor;
};

class Config {
public:
    static Config& instance();

    Config(const Config&)            = delete;
    Config& operator=(const Config&) = delete;

    bool load(const std::string& filePath = "config/netguard.json");
    bool save() const;

    const AppConfig& get()    const;
    AppConfig&       getMut();
    const std::string& filePath() const;

private:
    Config() = default;
    AppConfig   m_config;
    std::string m_filePath;
};

} // namespace NetGuard

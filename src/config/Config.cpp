// ============================================================
//  Config.cpp  v2
//  新增：vpn_interface、notify_cooldown_sec 的读写
// ============================================================

#include "Config.h"

#include <fstream>
#include <iostream>
#include <filesystem>
#include "../../third_party/nlohmann/json.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace NetGuard {

Config& Config::instance() {
    static Config inst;
    return inst;
}

bool Config::load(const std::string& filePath) {
    m_filePath = filePath;

    fs::path dir = fs::path(filePath).parent_path();
    if (!dir.empty() && !fs::exists(dir))
        fs::create_directories(dir);

    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cout << "[Config] 配置文件未找到，使用默认值并创建: " << filePath << "\n";
        save();
        return false;
    }

    try {
        json j;
        file >> j;

        // ---- 警报配置 ----
        if (j.contains("daily_limit_mb"))
            m_config.alert.dailyLimitMB = j["daily_limit_mb"].get<int>();
        if (j.contains("vpn_limit_mb"))
            m_config.alert.vpnLimitMB = j["vpn_limit_mb"].get<int>();
        if (j.contains("alert_threshold_percent"))
            m_config.alert.alertThresholdPercent = j["alert_threshold_percent"].get<int>();
        if (j.contains("warn_threshold_percent"))
            m_config.alert.warnThresholdPercent = j["warn_threshold_percent"].get<int>();
        if (j.contains("speed_alert_upload_kbps"))
            m_config.alert.speedAlertUploadKbps = j["speed_alert_upload_kbps"].get<double>();
        if (j.contains("speed_alert_download_kbps"))
            m_config.alert.speedAlertDownloadKbps = j["speed_alert_download_kbps"].get<double>();
        if (j.contains("notify_cooldown_sec"))
            m_config.alert.notifyCooldownSec = j["notify_cooldown_sec"].get<int>();

        // ---- 监控配置 ----
        if (j.contains("refresh_interval_ms"))
            m_config.monitor.refreshIntervalMs = j["refresh_interval_ms"].get<int>();
        if (j.contains("network_interface"))
            m_config.monitor.networkInterface = j["network_interface"].get<std::string>();
        if (j.contains("vpn_interface"))
            m_config.monitor.vpnInterface = j["vpn_interface"].get<std::string>();

        // ---- 窗口配置 ----
        if (j.contains("window")) {
            const auto& w = j["window"];
            if (w.contains("position_x")) m_config.window.positionX = w["position_x"].get<int>();
            if (w.contains("position_y")) m_config.window.positionY = w["position_y"].get<int>();
            if (w.contains("opacity"))    m_config.window.opacity   = w["opacity"].get<int>();
            if (w.contains("width"))      m_config.window.width     = w["width"].get<int>();
            if (w.contains("height"))     m_config.window.height    = w["height"].get<int>();
        }

        std::cout << "[Config] 配置加载成功: " << filePath << "\n";
        return true;

    } catch (const json::exception& e) {
        std::cerr << "[Config] JSON 解析失败: " << e.what() << "，使用默认值\n";
        m_config = AppConfig{};
        save();
        return false;
    }
}

bool Config::save() const {
    fs::path dir = fs::path(m_filePath).parent_path();
    if (!dir.empty() && !fs::exists(dir))
        fs::create_directories(dir);

    try {
        json j;
        j["daily_limit_mb"]              = m_config.alert.dailyLimitMB;
        j["vpn_limit_mb"]                = m_config.alert.vpnLimitMB;
        j["alert_threshold_percent"]     = m_config.alert.alertThresholdPercent;
        j["warn_threshold_percent"]      = m_config.alert.warnThresholdPercent;
        j["speed_alert_upload_kbps"]     = m_config.alert.speedAlertUploadKbps;
        j["speed_alert_download_kbps"]   = m_config.alert.speedAlertDownloadKbps;
        j["notify_cooldown_sec"]         = m_config.alert.notifyCooldownSec;
        j["refresh_interval_ms"]         = m_config.monitor.refreshIntervalMs;
        j["network_interface"]           = m_config.monitor.networkInterface;
        j["vpn_interface"]               = m_config.monitor.vpnInterface;
        j["window"]["position_x"]        = m_config.window.positionX;
        j["window"]["position_y"]        = m_config.window.positionY;
        j["window"]["opacity"]           = m_config.window.opacity;
        j["window"]["width"]             = m_config.window.width;
        j["window"]["height"]            = m_config.window.height;

        std::ofstream file(m_filePath);
        if (!file.is_open()) {
            std::cerr << "[Config] 无法写入配置文件: " << m_filePath << "\n";
            return false;
        }
        file << j.dump(4) << "\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Config] 保存配置失败: " << e.what() << "\n";
        return false;
    }
}

const AppConfig& Config::get()    const { return m_config; }
AppConfig&       Config::getMut()       { return m_config; }
const std::string& Config::filePath() const { return m_filePath; }

} // namespace NetGuard

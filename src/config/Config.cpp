// ============================================================
//  Config.cpp
//  职责：实现配置的读取、解析、写入逻辑
//  依赖：nlohmann/json（header-only，需放在 third_party/nlohmann/json.hpp）
// ============================================================

#include "Config.h"

#include <fstream>
#include <iostream>
#include <filesystem>

// nlohmann/json 单头文件库
// 下载地址：https://github.com/nlohmann/json/releases
// 将 json.hpp 放入项目 third_party/nlohmann/ 目录
#include "../../third_party/nlohmann/json.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace NetGuard {

// ============================================================
//  单例获取
// ============================================================
Config& Config::instance() {
    static Config inst;
    return inst;
}

// ============================================================
//  load() — 从 JSON 文件加载配置
// ============================================================
bool Config::load(const std::string& filePath) {
    m_filePath = filePath;

    // 确保配置目录存在
    fs::path dir = fs::path(filePath).parent_path();
    if (!dir.empty() && !fs::exists(dir)) {
        fs::create_directories(dir);
    }

    std::ifstream file(filePath);
    if (!file.is_open()) {
        // 文件不存在，使用默认值并生成文件
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

        // ---- 监控配置 ----
        if (j.contains("refresh_interval_ms"))
            m_config.monitor.refreshIntervalMs = j["refresh_interval_ms"].get<int>();

        if (j.contains("network_interface"))
            m_config.monitor.networkInterface = j["network_interface"].get<std::string>();

        // ---- 窗口配置 ----
        if (j.contains("window")) {
            const auto& w = j["window"];

            if (w.contains("position_x"))
                m_config.window.positionX = w["position_x"].get<int>();

            if (w.contains("position_y"))
                m_config.window.positionY = w["position_y"].get<int>();

            if (w.contains("opacity"))
                m_config.window.opacity = w["opacity"].get<int>();

            if (w.contains("width"))
                m_config.window.width = w["width"].get<int>();

            if (w.contains("height"))
                m_config.window.height = w["height"].get<int>();
        }

        std::cout << "[Config] 配置加载成功: " << filePath << "\n";
        return true;

    } catch (const json::exception& e) {
        std::cerr << "[Config] JSON 解析失败: " << e.what() << "，使用默认值\n";
        m_config = AppConfig{}; // 重置为默认
        save();                 // 覆盖损坏的文件
        return false;
    }
}

// ============================================================
//  save() — 将当前配置序列化写回 JSON 文件
// ============================================================
bool Config::save() const {
    // 确保目录存在
    fs::path dir = fs::path(m_filePath).parent_path();
    if (!dir.empty() && !fs::exists(dir)) {
        fs::create_directories(dir);
    }

    try {
        json j;

        // ---- 警报配置 ----
        j["daily_limit_mb"]              = m_config.alert.dailyLimitMB;
        j["vpn_limit_mb"]                = m_config.alert.vpnLimitMB;
        j["alert_threshold_percent"]     = m_config.alert.alertThresholdPercent;
        j["warn_threshold_percent"]      = m_config.alert.warnThresholdPercent;
        j["speed_alert_upload_kbps"]     = m_config.alert.speedAlertUploadKbps;
        j["speed_alert_download_kbps"]   = m_config.alert.speedAlertDownloadKbps;

        // ---- 监控配置 ----
        j["refresh_interval_ms"]         = m_config.monitor.refreshIntervalMs;
        j["network_interface"]           = m_config.monitor.networkInterface;

        // ---- 窗口配置 ----
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

        // 写入时保留 4 空格缩进，方便手动编辑
        file << j.dump(4) << "\n";
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[Config] 保存配置失败: " << e.what() << "\n";
        return false;
    }
}

// ============================================================
//  访问器
// ============================================================
const AppConfig& Config::get() const {
    return m_config;
}

AppConfig& Config::getMut() {
    return m_config;
}

const std::string& Config::filePath() const {
    return m_filePath;
}

} // namespace NetGuard

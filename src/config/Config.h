#pragma once

#include <string>

// ============================================================
//  Config.h
//  职责：定义所有配置项的数据结构，并声明 Config 类接口
//  Config 类负责从 netguard.json 读取配置，并提供全局访问入口
//  其他模块只需 #include "config/Config.h" 即可获取配置数据
// ============================================================

namespace NetGuard {

// ------------------------------------------------------------
//  窗口配置
// ------------------------------------------------------------
struct WindowConfig {
    int positionX   = 20;   // 置顶窗口初始 X 坐标（像素）
    int positionY   = 20;   // 置顶窗口初始 Y 坐标（像素）
    int opacity     = 200;  // 窗口透明度 0（全透明）~ 255（不透明）
    int width       = 260;  // 窗口宽度（像素）
    int height      = 160;  // 窗口高度（像素）
};

// ------------------------------------------------------------
//  警报配置
// ------------------------------------------------------------
struct AlertConfig {
    int    dailyLimitMB            = 1024;  // 每日流量限额（MB）
    int    vpnLimitMB              = 5120;  // VPN 套餐额度（MB），需手动维护
    int    alertThresholdPercent   = 80;    // 达到限额百分比时开始预警（%）
    int    warnThresholdPercent    = 95;    // 达到限额百分比时升级为警告（%）
    double speedAlertUploadKbps   = 1024.0; // 上传速度预警阈值（KB/s）
    double speedAlertDownloadKbps = 2048.0; // 下载速度预警阈值（KB/s）
};

// ------------------------------------------------------------
//  监控配置
// ------------------------------------------------------------
struct MonitorConfig {
    int         refreshIntervalMs  = 1000;  // 数据刷新间隔（毫秒）
    std::string networkInterface   = "auto"; // 监控的网卡名，"auto" 自动选择
};

// ------------------------------------------------------------
//  顶层配置容器（聚合所有子配置）
// ------------------------------------------------------------
struct AppConfig {
    WindowConfig  window;
    AlertConfig   alert;
    MonitorConfig monitor;
};

// ============================================================
//  Config 类
//  - 单例模式：全局唯一实例，通过 Config::instance() 获取
//  - load()  ：从文件加载配置，失败时使用默认值并生成文件
//  - save()  ：将当前配置写回文件
//  - get()   ：获取只读配置引用
//  - getMut()：获取可写配置引用（用于运行时修改）
// ============================================================
class Config {
public:
    // 获取单例
    static Config& instance();

    // 禁止拷贝和赋值
    Config(const Config&)            = delete;
    Config& operator=(const Config&) = delete;

    // 从指定路径加载配置文件
    // 若文件不存在或解析失败，使用默认值并自动创建文件
    // 返回 true 表示成功读取文件，false 表示使用了默认值
    bool load(const std::string& filePath = "config/netguard.json");

    // 将当前配置保存到文件（运行时修改后调用）
    bool save() const;

    // 获取只读配置
    const AppConfig& get() const;

    // 获取可写配置（修改后请调用 save()）
    AppConfig& getMut();

    // 返回配置文件路径
    const std::string& filePath() const;

private:
    Config() = default;

    AppConfig   m_config;           // 配置数据
    std::string m_filePath;         // 配置文件路径
};

} // namespace NetGuard

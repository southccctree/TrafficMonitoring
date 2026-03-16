#pragma once

// ============================================================
//  AlertManager.h
//  职责：对比当前流量数据与配置阈值，管理警报状态与等级
//        只负责"判断"，不负责"执行"（执行由 Notifier 负责）
//
//  警报等级（AlertLevel）：
//    NORMAL  — 用量低于限额的 alertThresholdPercent（默认 80%）
//    NOTICE  — 用量在 80% ～ 95% 之间
//    WARNING — 用量在 95% ～ 100% 之间
//    EXCEEDED— 用量超过 100% 限额
//    SPEED   — 上传或下载速度超过阈值（独立于流量等级）
// ============================================================

#include <string>
#include <cstdint>

namespace NetGuard {

// ------------------------------------------------------------
//  警报等级枚举
// ------------------------------------------------------------
enum class AlertLevel {
    NORMAL   = 0,  // 正常，无需提示
    NOTICE   = 1,  // 注意：接近限额
    WARNING  = 2,  // 警告：非常接近限额
    EXCEEDED = 3   // 超限：已超过每日限额
};

// ------------------------------------------------------------
//  速度警报状态（独立标志位，与流量等级并存）
// ------------------------------------------------------------
struct SpeedAlertFlags {
    bool uploadExceeded   = false;  // 上传速度超阈值
    bool downloadExceeded = false;  // 下载速度超阈值
};

// ------------------------------------------------------------
//  完整警报状态快照
// ------------------------------------------------------------
struct AlertStatus {
    AlertLevel      level       = AlertLevel::NORMAL;
    SpeedAlertFlags speedFlags;

    double usagePercent   = 0.0;   // 今日用量占每日限额的百分比
    double dailyUsedMB    = 0.0;   // 今日已用 MB
    double dailyLimitMB   = 0.0;   // 每日限额 MB
    double vpnUsedMB      = 0.0;   // VPN 今日用量 MB（与每日用量相同）
    double vpnLimitMB     = 0.0;   // VPN 套餐限额 MB

    double currentUploadKBps   = 0.0;  // 当前上传速度
    double currentDownloadKBps = 0.0;  // 当前下载速度

    // 是否需要触发通知（等级变化时为 true）
    bool levelChanged = false;
    bool speedChanged = false;

    // 当前等级对应的描述文字
    std::string levelDescription() const;
};

// ============================================================
//  AlertManager 类
//  - evaluate() ：传入最新数据，更新并返回警报状态
//  - status()   ：获取上一次 evaluate() 的结果
// ============================================================
class AlertManager {
public:
    AlertManager()  = default;
    ~AlertManager() = default;

    // 传入最新的流量和速度数据，计算并返回当前警报状态
    // dailyUsedMB       : 今日累计用量（MB）
    // vpnUsedMB         : VPN 今日用量（MB），通常与 dailyUsedMB 相同
    // uploadKBps        : 当前上传速度（KB/s）
    // downloadKBps      : 当前下载速度（KB/s）
    AlertStatus evaluate(double dailyUsedMB,
                         double vpnUsedMB,
                         double uploadKBps,
                         double downloadKBps);

    // 获取上一次评估的警报状态
    const AlertStatus& status() const;

private:
    // 根据用量百分比计算警报等级
    AlertLevel calcLevel(double usagePercent) const;

    AlertStatus m_status;   // 上一次的警报状态（用于检测变化）
};

} // namespace NetGuard

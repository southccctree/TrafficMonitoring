// ============================================================
//  AlertManager.cpp
//  职责：实现警报等级的判断逻辑，检测状态变化
//  依赖：Config（读取阈值配置）
// ============================================================

#include "AlertManager.h"
#include "../config/Config.h"

#include <algorithm>

namespace NetGuard {

// ============================================================
//  evaluate() — 传入最新数据，计算并返回当前警报状态
// ============================================================
AlertStatus AlertManager::evaluate(double dailyUsedMB,
                                   double vpnUsedMB,
                                   double uploadKBps,
                                   double downloadKBps)
{
    const auto& cfg = Config::instance().get().alert;

    AlertStatus next;

    // ---- 填入原始数据 ----
    next.dailyUsedMB          = dailyUsedMB;
    next.dailyLimitMB         = static_cast<double>(cfg.dailyLimitMB);
    next.vpnUsedMB            = vpnUsedMB;
    next.vpnLimitMB           = static_cast<double>(cfg.vpnLimitMB);
    next.currentUploadKBps    = uploadKBps;
    next.currentDownloadKBps  = downloadKBps;

    // ---- 计算今日用量百分比 ----
    // 取每日限额和 VPN 限额中较小的那个作为有效上限
    // 这样无论哪个先到达都能触发预警
    double effectiveLimit = std::min(next.dailyLimitMB, next.vpnLimitMB);
    if (effectiveLimit > 0.0) {
        next.usagePercent = (dailyUsedMB / effectiveLimit) * 100.0;
    } else {
        next.usagePercent = 0.0;
    }

    // ---- 判断流量警报等级 ----
    next.level = calcLevel(next.usagePercent);

    // ---- 判断速度警报 ----
    next.speedFlags.uploadExceeded =
        (uploadKBps > cfg.speedAlertUploadKbps);
    next.speedFlags.downloadExceeded =
        (downloadKBps > cfg.speedAlertDownloadKbps);

    // ---- 检测状态是否发生变化（用于决定是否触发通知）----
    next.levelChanged =
        (next.level != m_status.level);

    next.speedChanged =
        (next.speedFlags.uploadExceeded   != m_status.speedFlags.uploadExceeded) ||
        (next.speedFlags.downloadExceeded != m_status.speedFlags.downloadExceeded);

    // 保存本次状态
    m_status = next;
    return m_status;
}

// ============================================================
//  status() — 返回上一次评估结果
// ============================================================
const AlertStatus& AlertManager::status() const {
    return m_status;
}

// ============================================================
//  calcLevel() — 根据用量百分比返回警报等级
// ============================================================
AlertLevel AlertManager::calcLevel(double usagePercent) const {
    const auto& cfg = Config::instance().get().alert;

    if (usagePercent >= 100.0) {
        return AlertLevel::EXCEEDED;
    } else if (usagePercent >= static_cast<double>(cfg.warnThresholdPercent)) {
        return AlertLevel::WARNING;
    } else if (usagePercent >= static_cast<double>(cfg.alertThresholdPercent)) {
        return AlertLevel::NOTICE;
    } else {
        return AlertLevel::NORMAL;
    }
}

// ============================================================
//  AlertStatus::levelDescription() — 等级对应的描述文字
// ============================================================
std::string AlertStatus::levelDescription() const {
    switch (level) {
        case AlertLevel::NORMAL:
            return "正常";
        case AlertLevel::NOTICE:
            return "注意：流量接近限额";
        case AlertLevel::WARNING:
            return "警告：流量即将耗尽";
        case AlertLevel::EXCEEDED:
            return "超限：今日流量已超出限额！";
        default:
            return "未知";
    }
}

} // namespace NetGuard

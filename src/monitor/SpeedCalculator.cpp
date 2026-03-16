// ============================================================
//  SpeedCalculator.cpp
//  职责：实现两次快照之间的速度差值计算与格式化输出
// ============================================================

#include "SpeedCalculator.h"

#include <sstream>
#include <iomanip>
#include <algorithm>

namespace NetGuard {

// ============================================================
//  update() — 传入最新快照，计算与上一次的速度差
// ============================================================
void SpeedCalculator::update(const InterfaceSnapshot& current) {
    auto now = std::chrono::steady_clock::now();

    if (!m_hasHistory) {
        // 第一次调用：只保存快照，无法计算差值
        m_prevSnapshot = current;
        m_prevTime     = now;
        m_hasHistory   = true;
        m_deltaUpload   = 0;
        m_deltaDownload = 0;
        m_latest        = SpeedResult{};
        return;
    }

    // 计算时间间隔（秒）
    double elapsed = std::chrono::duration<double>(now - m_prevTime).count();

    // 防止除以零或时间倒退（极少数情况）
    if (elapsed < 0.001) return;

    // 计算字节差值（注意：系统重启或网卡重置时计数器可能归零）
    // 若新值小于旧值，视为计数器溢出/重置，本次差值置零
    uint64_t rawUpload   = current.bytesSent;
    uint64_t rawDownload = current.bytesReceived;
    uint64_t prevUpload   = m_prevSnapshot.bytesSent;
    uint64_t prevDownload = m_prevSnapshot.bytesReceived;

    m_deltaUpload   = (rawUpload   >= prevUpload)   ? (rawUpload   - prevUpload)   : 0;
    m_deltaDownload = (rawDownload >= prevDownload) ? (rawDownload - prevDownload) : 0;

    // 计算速度（字节/秒）
    m_latest.uploadBytesPerSec   = static_cast<double>(m_deltaUpload)   / elapsed;
    m_latest.downloadBytesPerSec = static_cast<double>(m_deltaDownload) / elapsed;

    // 更新历史快照
    m_prevSnapshot = current;
    m_prevTime     = now;
}

// ============================================================
//  latest() — 获取最近一次速度结果
// ============================================================
const SpeedResult& SpeedCalculator::latest() const {
    return m_latest;
}

// ============================================================
//  reset() — 清除历史，下次从头开始
// ============================================================
void SpeedCalculator::reset() {
    m_hasHistory    = false;
    m_deltaUpload   = 0;
    m_deltaDownload = 0;
    m_latest        = SpeedResult{};
}

// ============================================================
//  formatSpeed() — 自动选单位格式化速度字符串
// ============================================================
std::string SpeedCalculator::formatSpeed(double bytesPerSec) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1);

    if (bytesPerSec >= 1024.0 * 1024.0) {
        oss << (bytesPerSec / (1024.0 * 1024.0)) << " MB/s";
    } else if (bytesPerSec >= 1024.0) {
        oss << (bytesPerSec / 1024.0) << " KB/s";
    } else {
        oss << bytesPerSec << " B/s";
    }

    return oss.str();
}

// ============================================================
//  SpeedResult 格式化方法实现
// ============================================================
std::string SpeedResult::uploadFormatted() const {
    return SpeedCalculator::formatSpeed(uploadBytesPerSec);
}

std::string SpeedResult::downloadFormatted() const {
    return SpeedCalculator::formatSpeed(downloadBytesPerSec);
}

} // namespace NetGuard

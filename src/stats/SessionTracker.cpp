// ============================================================
//  SessionTracker.cpp
//  职责：实现会话流量统计与运行时长计算
// ============================================================

#include "SessionTracker.h"

#include <sstream>
#include <iomanip>

namespace NetGuard {

// ============================================================
//  start() — 记录会话开始时间
// ============================================================
void SessionTracker::start() {
    m_startTime    = std::chrono::steady_clock::now();
    m_uploadBytes   = 0;
    m_downloadBytes = 0;
    m_started       = true;
}

// ============================================================
//  addBytes() — 追加字节增量
// ============================================================
void SessionTracker::addBytes(uint64_t uploadDelta, uint64_t downloadDelta) {
    if (!m_started) return;
    m_uploadBytes   += uploadDelta;
    m_downloadBytes += downloadDelta;
}

// ============================================================
//  snapshot() — 生成当前会话快照
// ============================================================
SessionRecord SessionTracker::snapshot() const {
    SessionRecord rec;
    rec.uploadBytes   = m_uploadBytes;
    rec.downloadBytes = m_downloadBytes;

    if (m_started) {
        auto now = std::chrono::steady_clock::now();
        rec.elapsedSeconds =
            std::chrono::duration<double>(now - m_startTime).count();
    }

    return rec;
}

// ============================================================
//  reset() — 手动归零并重置计时器
// ============================================================
void SessionTracker::reset() {
    start(); // 重新记录开始时间，同时清零字节数
}

// ============================================================
//  SessionRecord 方法实现
// ============================================================

double SessionRecord::avgUploadKBps() const {
    if (elapsedSeconds < 0.001) return 0.0;
    return (uploadBytes / 1024.0) / elapsedSeconds;
}

double SessionRecord::avgDownloadKBps() const {
    if (elapsedSeconds < 0.001) return 0.0;
    return (downloadBytes / 1024.0) / elapsedSeconds;
}

std::string SessionRecord::elapsedFormatted() const {
    auto total = static_cast<uint64_t>(elapsedSeconds);

    uint64_t hours   = total / 3600;
    uint64_t minutes = (total % 3600) / 60;
    uint64_t seconds = total % 60;

    std::ostringstream oss;

    if (hours > 0) {
        oss << hours << "h ";
    }
    if (hours > 0 || minutes > 0) {
        oss << minutes << "m ";
    }
    oss << seconds << "s";

    return oss.str();
}

} // namespace NetGuard

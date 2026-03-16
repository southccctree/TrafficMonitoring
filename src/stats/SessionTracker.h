#pragma once

// ============================================================
//  SessionTracker.h
//  职责：记录本次程序运行期间的流量消耗
//        程序关闭后数据不保留，不涉及文件读写
//
//  与 DailyTracker 的区别：
//    - SessionTracker 只活在内存里，关闭即清零
//    - 用途：让用户看到"这次开机/这次挂 VPN 用了多少"
//    - 额外提供运行时长、平均速度等会话维度的统计
// ============================================================

#include <cstdint>
#include <string>
#include <chrono>

namespace NetGuard {

// ------------------------------------------------------------
//  会话统计快照（只读输出用）
// ------------------------------------------------------------
struct SessionRecord {
    uint64_t uploadBytes   = 0;  // 本次会话累计上传字节
    uint64_t downloadBytes = 0;  // 本次会话累计下载字节

    double elapsedSeconds  = 0.0; // 会话已运行秒数

    // 换算
    double uploadMB()   const { return uploadBytes   / (1024.0 * 1024.0); }
    double downloadMB() const { return downloadBytes / (1024.0 * 1024.0); }
    double totalMB()    const { return (uploadBytes + downloadBytes) / (1024.0 * 1024.0); }

    // 平均速度（整个会话期间）
    double avgUploadKBps()   const;
    double avgDownloadKBps() const;

    // 格式化运行时长，例如 "1h 23m 45s"
    std::string elapsedFormatted() const;
};

// ============================================================
//  SessionTracker 类
//  - start()    ：记录会话开始时间（程序启动时调用一次）
//  - addBytes() ：追加本次刷新的字节增量（每次主循环调用）
//  - snapshot() ：获取当前会话统计快照
//  - reset()    ：手动归零（用户可在 UI 中触发）
// ============================================================
class SessionTracker {
public:
    SessionTracker()  = default;
    ~SessionTracker() = default;

    // 记录会话开始时间，程序启动时调用一次
    void start();

    // 追加字节增量
    void addBytes(uint64_t uploadDelta, uint64_t downloadDelta);

    // 获取当前会话快照（含运行时长、平均速度等）
    SessionRecord snapshot() const;

    // 手动归零并重置计时器
    void reset();

    // 会话是否已启动
    bool isStarted() const { return m_started; }

private:
    uint64_t   m_uploadBytes   = 0;
    uint64_t   m_downloadBytes = 0;
    bool       m_started       = false;

    std::chrono::steady_clock::time_point m_startTime;
};

} // namespace NetGuard

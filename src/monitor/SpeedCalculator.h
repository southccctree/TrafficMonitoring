#pragma once

// ============================================================
//  SpeedCalculator.h
//  职责：对 NetworkMonitor 提供的两次字节快照做差值计算
//        输出当前上传 / 下载速度，以及格式化的可读字符串
//
//  工作原理：
//    speed = (bytes_now - bytes_prev) / elapsed_seconds
//  每次调用 update() 传入最新快照，内部保存上一次快照
//  首次调用无法计算差值，返回速度为 0
// ============================================================

#include "NetworkMonitor.h"

#include <string>
#include <chrono>
#include <cstdint>

namespace NetGuard {

// ------------------------------------------------------------
//  一次计算结果：包含上传/下载速度及格式化字符串
// ------------------------------------------------------------
struct SpeedResult {
    double uploadBytesPerSec   = 0.0;  // 上传速度（字节/秒）
    double downloadBytesPerSec = 0.0;  // 下载速度（字节/秒）

    // 便捷换算
    double uploadKBps()   const { return uploadBytesPerSec   / 1024.0; }
    double downloadKBps() const { return downloadBytesPerSec / 1024.0; }
    double uploadMBps()   const { return uploadBytesPerSec   / (1024.0 * 1024.0); }
    double downloadMBps() const { return downloadBytesPerSec / (1024.0 * 1024.0); }

    // 自动选择合适单位的格式化字符串，例如 "1.23 MB/s" 或 "512.0 KB/s"
    std::string uploadFormatted()   const;
    std::string downloadFormatted() const;
};

// ============================================================
//  SpeedCalculator 类
//  - update()    ：传入最新快照，触发一次速度计算
//  - latest()    ：获取最近一次计算结果
//  - reset()     ：清除历史快照（切换网卡时使用）
//  - deltaBytes()：返回本次与上次快照之间的字节差值（用于累计统计）
// ============================================================
class SpeedCalculator {
public:
    SpeedCalculator()  = default;
    ~SpeedCalculator() = default;

    // 传入最新的网卡快照，更新速度计算
    // 第一次调用由于没有历史数据，速度结果为 0
    void update(const InterfaceSnapshot& current);

    // 获取最近一次计算出的速度结果
    const SpeedResult& latest() const;

    // 返回本次更新相对于上次的字节增量
    // upload   = 上传新增字节数
    // download = 下载新增字节数
    // 用于 DailyTracker 和 SessionTracker 累计统计
    uint64_t deltaUpload()   const { return m_deltaUpload;   }
    uint64_t deltaDownload() const { return m_deltaDownload; }

    // 是否已有足够数据（至少经历过一次完整的两点差值计算）
    bool isReady() const { return m_hasHistory; }

    // 清除历史快照，下次 update() 会重新开始
    void reset();

    // 格式化速度为带单位字符串（SpeedResult 的 formatted() 方法也会调用）
    static std::string formatSpeed(double bytesPerSec);

private:

    SpeedResult     m_latest;           // 最新速度结果
    InterfaceSnapshot m_prevSnapshot;   // 上一次快照（用于差值）
    std::chrono::steady_clock::time_point m_prevTime; // 上一次采集时间

    uint64_t m_deltaUpload   = 0;       // 本次上传字节增量
    uint64_t m_deltaDownload = 0;       // 本次下载字节增量

    bool m_hasHistory = false;          // 是否已有历史数据
};

} // namespace NetGuard

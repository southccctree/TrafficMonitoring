#pragma once

// ============================================================
//  DailyTracker.h
//  职责：以自然日为周期累计上传 / 下载流量
//        负责数据的持久化读写（daily_usage.json）
//        每次程序启动时读取当日已有数据，次日自动归零
//
//  与 SessionTracker 的区别：
//    - DailyTracker  跨程序重启保留数据，以"天"为归零周期
//    - SessionTracker 只记录本次运行，程序关闭即丢弃
// ============================================================

#include <string>
#include <cstdint>

namespace NetGuard {

// ------------------------------------------------------------
//  每日流量记录
// ------------------------------------------------------------
struct DailyRecord {
    std::string date;           // 日期字符串，格式 "YYYY-MM-DD"
    uint64_t    uploadBytes   = 0;  // 今日累计上传字节
    uint64_t    downloadBytes = 0;  // 今日累计下载字节

    // 便捷换算
    double uploadMB()   const { return uploadBytes   / (1024.0 * 1024.0); }
    double downloadMB() const { return downloadBytes / (1024.0 * 1024.0); }
    double totalMB()    const { return (uploadBytes + downloadBytes) / (1024.0 * 1024.0); }
};

// ============================================================
//  DailyTracker 类
//  - load()       ：从文件加载今日记录，若日期不匹配则归零
//  - save()       ：将当前记录写入文件
//  - addBytes()   ：追加本次刷新的字节增量
//  - record()     ：获取当前记录的只读引用
//  - checkRollover()：检查是否跨日，若跨日则自动归零并保存
// ============================================================
class DailyTracker {
public:
    DailyTracker()  = default;
    ~DailyTracker() = default;

    // 从指定路径加载数据文件
    // 若文件不存在或日期不是今天，自动归零并创建
    bool load(const std::string& filePath = "data/daily_usage.json");

    // 将当前记录持久化到文件
    bool save() const;

    // 追加字节增量（每次主循环刷新时调用）
    void addBytes(uint64_t uploadDelta, uint64_t downloadDelta);

    // 检查是否已跨日，若是则归零并保存
    // 返回 true 表示发生了跨日归零
    bool checkRollover();

    // 获取当前每日记录
    const DailyRecord& record() const;

    // 返回数据文件路径
    const std::string& filePath() const;

private:
    // 获取今天的日期字符串 "YYYY-MM-DD"
    static std::string todayString();

    DailyRecord m_record;       // 当前每日记录
    std::string m_filePath;     // 数据文件路径
};

} // namespace NetGuard

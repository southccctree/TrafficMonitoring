// ============================================================
//  DailyTracker.cpp
//  职责：实现每日流量的持久化读写、归零逻辑
//  依赖：nlohmann/json
// ============================================================

#include "DailyTracker.h"

#include <fstream>
#include <iostream>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "../../third_party/nlohmann/json.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace NetGuard {

// ============================================================
//  load() — 加载今日数据，日期不匹配则归零
// ============================================================
bool DailyTracker::load(const std::string& filePath) {
    m_filePath = filePath;

    // 确保数据目录存在
    fs::path dir = fs::path(filePath).parent_path();
    if (!dir.empty() && !fs::exists(dir)) {
        fs::create_directories(dir);
    }

    std::string today = todayString();

    std::ifstream file(filePath);
    if (!file.is_open()) {
        // 文件不存在，初始化今日记录
        std::cout << "[DailyTracker] 数据文件未找到，初始化今日记录\n";
        m_record = DailyRecord{ today, 0, 0 };
        save();
        return false;
    }

    try {
        json j;
        file >> j;

        std::string savedDate = j.value("date", "");

        if (savedDate != today) {
            // 日期不匹配：已是新的一天，归零
            std::cout << "[DailyTracker] 检测到新的一天（" << today
                      << "），流量数据已归零\n";
            m_record = DailyRecord{ today, 0, 0 };
            save();
            return true;
        }

        // 日期匹配：加载已有数据继续累计
        m_record.date          = savedDate;
        m_record.uploadBytes   = j.value("upload_bytes",   uint64_t(0));
        m_record.downloadBytes = j.value("download_bytes", uint64_t(0));

        std::cout << "[DailyTracker] 今日数据已加载 — "
                  << "上传: " << m_record.uploadMB()   << " MB, "
                  << "下载: " << m_record.downloadMB() << " MB\n";
        return true;

    } catch (const json::exception& e) {
        std::cerr << "[DailyTracker] JSON 解析失败: " << e.what()
                  << "，数据已归零\n";
        m_record = DailyRecord{ today, 0, 0 };
        save();
        return false;
    }
}

// ============================================================
//  save() — 将当前记录序列化写回文件
// ============================================================
bool DailyTracker::save() const {
    // 确保目录存在
    fs::path dir = fs::path(m_filePath).parent_path();
    if (!dir.empty() && !fs::exists(dir)) {
        fs::create_directories(dir);
    }

    try {
        json j;
        j["date"]           = m_record.date;
        j["upload_bytes"]   = m_record.uploadBytes;
        j["download_bytes"] = m_record.downloadBytes;
        // 同时保存可读的 MB 值，方便手动查看
        j["upload_mb"]      = m_record.uploadMB();
        j["download_mb"]    = m_record.downloadMB();
        j["total_mb"]       = m_record.totalMB();

        std::ofstream file(m_filePath);
        if (!file.is_open()) {
            std::cerr << "[DailyTracker] 无法写入数据文件: " << m_filePath << "\n";
            return false;
        }
        file << j.dump(4) << "\n";
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[DailyTracker] 保存失败: " << e.what() << "\n";
        return false;
    }
}

// ============================================================
//  addBytes() — 追加本次刷新的字节增量
// ============================================================
void DailyTracker::addBytes(uint64_t uploadDelta, uint64_t downloadDelta) {
    m_record.uploadBytes   += uploadDelta;
    m_record.downloadBytes += downloadDelta;
}

// ============================================================
//  checkRollover() — 检查是否跨日，若是则归零
// ============================================================
bool DailyTracker::checkRollover() {
    std::string today = todayString();
    if (m_record.date == today) return false;

    std::cout << "[DailyTracker] 跨日归零：" << m_record.date
              << " → " << today << "\n";
    m_record = DailyRecord{ today, 0, 0 };
    save();
    return true;
}

// ============================================================
//  record() — 只读访问当前记录
// ============================================================
const DailyRecord& DailyTracker::record() const {
    return m_record;
}

const std::string& DailyTracker::filePath() const {
    return m_filePath;
}

// ============================================================
//  todayString() — 返回今天的 "YYYY-MM-DD" 字符串
// ============================================================
std::string DailyTracker::todayString() {
    auto now     = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm     tm{};

#ifdef _WIN32
    localtime_s(&tm, &t);   // Windows 线程安全版本
#else
    localtime_r(&t, &tm);   // POSIX 线程安全版本
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d");
    return oss.str();
}

} // namespace NetGuard

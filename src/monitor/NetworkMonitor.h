#pragma once

// ============================================================
//  NetworkMonitor.h
//  职责：通过 Windows API 读取系统网卡的原始字节计数
//        只负责"取数据"，不做任何计算
//
//  核心 API：GetIfTable2()（iphlpapi）
//  返回值：每块网卡当前累计收发的原始字节数（自系统启动起累计）
//  上层模块（SpeedCalculator）对两次快照做差值，得出速度
// ============================================================

#include <string>
#include <vector>
#include <cstdint>

// Windows 网络相关头文件
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <iphlpapi.h>
// 链接库：在 CMakeLists.txt 中添加 iphlpapi
// target_link_libraries(NetGuard PRIVATE iphlpapi)

namespace NetGuard {

// ------------------------------------------------------------
//  单块网卡的快照数据
// ------------------------------------------------------------
struct InterfaceSnapshot {
    std::wstring name;          // 网卡名称（宽字符，Windows 原生格式）
    std::string  nameUtf8;      // 网卡名称（UTF-8，用于日志和配置匹配）
    uint64_t     bytesSent;     // 累计发送字节数（上传）
    uint64_t     bytesReceived; // 累计接收字节数（下载）
    bool         isUp;          // 网卡是否处于连接状态
};

// ------------------------------------------------------------
//  NetworkMonitor 类
//  - snapshot()     ：采集所有网卡当前字节数，返回快照列表
//  - snapshotTotal()：仅返回指定网卡（或自动选择）的合并快照
//  - listInterfaces()：列出所有可用网卡名称（用于配置和调试）
//  - selectInterface()：根据配置中的网卡名自动匹配或选择流量最大的
// ------------------------------------------------------------
class NetworkMonitor {
public:
    NetworkMonitor()  = default;
    ~NetworkMonitor() = default;

    // 采集所有网卡的字节快照
    // 返回 false 表示 API 调用失败
    bool snapshot(std::vector<InterfaceSnapshot>& out) const;

    // 返回指定网卡的单条快照
    // interfaceName 为 "auto" 时自动选择当前字节数最大的网卡
    // 返回 false 表示未找到目标网卡或 API 失败
    bool snapshotSingle(const std::string& interfaceName,
                        InterfaceSnapshot& out) const;

    // 列出所有网卡的 UTF-8 名称，用于调试或配置时选择
    std::vector<std::string> listInterfaces() const;

private:
    // 将宽字符网卡名转换为 UTF-8
    static std::string wideToUtf8(const std::wstring& wide);

    // 从所有快照中找出累计字节数最大的网卡（最可能是活跃网卡）
    static const InterfaceSnapshot* findBusiest(
        const std::vector<InterfaceSnapshot>& list);
};

} // namespace NetGuard

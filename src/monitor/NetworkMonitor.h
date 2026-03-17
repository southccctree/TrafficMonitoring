#pragma once

// ============================================================
//  NetworkMonitor.h  v2
//  职责：通过 Npcap 逐包捕获，统计公网流量
//
//  v2 新增/修复：
//    1. immediate mode   — 数据包到达即上报，消除延迟
//    2. IPv6 支持        — 视频/QUIC 等 IPv6 流量不再漏统计
//    3. filterLan 开关   — false 时统计全量流量（VPN 网卡复用）
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <netioapi.h>
#include <iphlpapi.h>
#include <pcap.h>

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <cstdint>

namespace NetGuard {

// ------------------------------------------------------------
//  网卡快照（供 SpeedCalculator 使用，接口不变）
// ------------------------------------------------------------
struct InterfaceSnapshot {
    std::string nameUtf8;
    uint64_t    bytesSent     = 0;
    uint64_t    bytesReceived = 0;
    bool        isUp          = true;
};

// ============================================================
//  NetworkMonitor 类
// ============================================================
class NetworkMonitor {
public:
    NetworkMonitor()  = default;
    ~NetworkMonitor() { stop(); }

    // 启动抓包线程
    // interfaceName : "auto" 自动选择，或具体网卡名
    // filterLan     : true=只统计公网（默认），false=统计全量（VPN网卡用）
    bool start(const std::string& interfaceName = "auto",
               bool               filterLan     = true);

    void stop();

    // 获取当前字节计数快照（兼容原版接口）
    bool snapshotSingle(const std::string& interfaceName,
                        InterfaceSnapshot& out) const;

    // 列出所有 Npcap 可见网卡
    std::vector<std::string> listInterfaces() const;

    bool isRunning() const { return m_running.load(); }

private:
    void captureLoop();
    void processPacket(const u_char* data, int capLen);

    static bool     isPrivateIPv4(uint32_t ipNetOrder);
    static bool     isPrivateIPv6(const uint8_t ip[16]);
    static uint32_t getLocalIPv4(const std::string& pcapDevName);
    std::string     autoSelectInterface() const;

    std::atomic<uint64_t> m_bytesSent    { 0 };
    std::atomic<uint64_t> m_bytesReceived{ 0 };

    std::string  m_interfaceName;
    uint32_t     m_localIPv4 = 0;
    bool         m_filterLan = true;

    std::thread       m_thread;
    std::atomic<bool> m_running  { false };
    std::atomic<bool> m_stopFlag { false };

    pcap_t* m_handle = nullptr;
};

} // namespace NetGuard

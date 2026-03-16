#pragma once

// ============================================================
//  NetworkMonitor.h
//  职责：通过 Npcap 捕获数据包，只统计公网流量
//        自动过滤局域网（LAN）流量，包括内网串流等
//
//  与原版的区别：
//    原版：读取网卡累计字节总数（含局域网）
//    新版：逐包捕获，跳过源和目标都是私有 IP 的数据包
//
//  私有地址段（直接跳过）：
//    10.0.0.0/8
//    172.16.0.0/12
//    192.168.0.0/16
//    127.0.0.0/8    （回环）
//    169.254.0.0/16 （链路本地）
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

// Npcap SDK 头文件
// 将 Npcap SDK 解压至 third_party/npcap-sdk/
#include <pcap.h>

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <cstdint>

namespace NetGuard {

// ------------------------------------------------------------
//  网卡快照（与原版保持相同接口，SpeedCalculator 无需修改）
// ------------------------------------------------------------
struct InterfaceSnapshot {
    std::string nameUtf8;           // 网卡名称
    uint64_t    bytesSent     = 0;  // 累计公网上传字节（不含 LAN）
    uint64_t    bytesReceived = 0;  // 累计公网下载字节（不含 LAN）
    bool        isUp          = true;
};

// ============================================================
//  NetworkMonitor 类
//  - start()          ：启动后台抓包线程
//  - stop()           ：停止抓包
//  - snapshotSingle() ：返回当前公网字节计数快照
//  - listInterfaces() ：列出所有可用网卡（供配置选择）
//  - isRunning()      ：是否已启动
// ============================================================
class NetworkMonitor {
public:
    NetworkMonitor()  = default;
    ~NetworkMonitor() { stop(); }

    // 启动抓包线程
    // interfaceName："auto" 自动选择，或传入具体网卡名
    // 返回 false 表示 Npcap 未安装或网卡不存在
    bool start(const std::string& interfaceName = "auto");

    // 停止抓包线程，释放 Npcap 句柄
    void stop();

    // 获取当前公网字节计数快照（兼容原版接口）
    bool snapshotSingle(const std::string& interfaceName,
                        InterfaceSnapshot& out) const;

    // 列出所有 Npcap 可见的网卡名称
    std::vector<std::string> listInterfaces() const;

    // 是否已成功启动抓包
    bool isRunning() const { return m_running.load(); }

private:
    // 后台抓包线程：持续调用 pcap_next_ex 获取数据包
    void captureLoop();

    // 处理单个数据包：解析以太网帧 + IP 头，过滤 LAN 流量
    void processPacket(const u_char* data, int capLen);

    // 判断 IP（网络字节序 uint32_t）是否属于私有地址段
    static bool isPrivateIP(uint32_t ipNetOrder);

    // 从系统获取指定网卡的本机 IP（用于判断上传/下载方向）
    static uint32_t getLocalIP(const std::string& pcapDevName);

    // 自动选择最合适的物理网卡（排除虚拟/回环网卡）
    std::string autoSelectInterface() const;

    // ---- 统计计数器（原子，线程安全）----
    std::atomic<uint64_t> m_bytesSent    { 0 };  // 公网上传
    std::atomic<uint64_t> m_bytesReceived{ 0 };  // 公网下载

    std::string m_interfaceName;    // 实际使用的网卡 pcap 设备名
    std::string m_interfaceDesc;    // 网卡描述（用于日志）
    uint32_t    m_localIP = 0;      // 本机 IP（网络字节序）

    std::thread       m_thread;
    std::atomic<bool> m_running  { false };
    std::atomic<bool> m_stopFlag { false };

    pcap_t* m_handle = nullptr;     // Npcap 捕获句柄
};

} // namespace NetGuard

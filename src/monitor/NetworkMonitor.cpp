// ============================================================
//  NetworkMonitor.cpp
//  职责：用 Npcap 逐包捕获，过滤局域网流量，只统计公网字节
//  依赖：Npcap SDK（third_party/npcap-sdk/）、iphlpapi
// ============================================================

#include "NetworkMonitor.h"

#include <iostream>
#include <algorithm>
#include <cstring>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
// wpcap.lib 和 Packet.lib 在 tasks.json 的 -L 参数中指定

namespace NetGuard {

// ============================================================
//  以太网帧头（14 字节）
// ============================================================
#pragma pack(push, 1)
struct EtherHeader {
    uint8_t  dstMac[6];
    uint8_t  srcMac[6];
    uint16_t etherType;   // 网络字节序，0x0800 = IPv4
};

// IPv4 头（最小 20 字节）
struct IPv4Header {
    uint8_t  versionIHL;  // 高4位版本(4)，低4位头长度（×4字节）
    uint8_t  tos;
    uint16_t totalLength; // 网络字节序，含头+数据
    uint16_t id;
    uint16_t flagsOffset;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t srcIP;       // 网络字节序
    uint32_t dstIP;       // 网络字节序
};
#pragma pack(pop)

static constexpr uint16_t ETHERTYPE_IPV4 = 0x0800;

// ============================================================
//  start() — 初始化 Npcap，启动抓包线程
// ============================================================
bool NetworkMonitor::start(const std::string& interfaceName) {
    if (m_running.load()) return true;

    // 确定要监控的网卡
    std::string devName = (interfaceName == "auto")
        ? autoSelectInterface()
        : interfaceName;

    if (devName.empty()) {
        std::cerr << "[NetworkMonitor] 未找到合适的网卡\n";
        return false;
    }

    // 获取本机 IP（用于方向判断）
    m_localIP = getLocalIP(devName);

    // 打开网卡（混杂模式关闭，只捕获本机收发的包）
    char errbuf[PCAP_ERRBUF_SIZE] = {};
    m_handle = pcap_open_live(
        devName.c_str(),
        65535,      // 最大捕获字节数
        0,          // 不开启混杂模式（只看本机流量）
        100,        // 读超时 ms
        errbuf
    );

    if (!m_handle) {
        std::cerr << "[NetworkMonitor] pcap_open_live 失败: " << errbuf << "\n";
        return false;
    }

    // 只捕获 IPv4 数据包（过滤器），排除 ARP、IPv6 等
    struct bpf_program fp;
    if (pcap_compile(m_handle, &fp, "ip", 0, PCAP_NETMASK_UNKNOWN) == 0) {
        pcap_setfilter(m_handle, &fp);
        pcap_freecode(&fp);
    }

    m_interfaceName = devName;
    m_stopFlag      = false;
    m_running       = true;

    std::cout << "[NetworkMonitor] 开始监控（公网流量过滤模式）: "
              << devName << "\n";
    if (m_localIP != 0) {
        in_addr addr;
        addr.s_addr = m_localIP;
        std::cout << "[NetworkMonitor] 本机 IP: "
                  << inet_ntoa(addr) << "\n";
    }

    // 启动后台捕获线程
    m_thread = std::thread([this]() { captureLoop(); });
    return true;
}

// ============================================================
//  stop() — 停止抓包，释放资源
// ============================================================
void NetworkMonitor::stop() {
    if (!m_running.load()) return;

    m_stopFlag = true;

    // 中断 pcap_next_ex 的阻塞
    if (m_handle) {
        pcap_breakloop(m_handle);
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }

    if (m_handle) {
        pcap_close(m_handle);
        m_handle = nullptr;
    }

    m_running = false;
    std::cout << "[NetworkMonitor] 已停止\n";
}

// ============================================================
//  snapshotSingle() — 返回当前公网字节计数快照
//  接口与原版完全相同，SpeedCalculator 无需修改
// ============================================================
bool NetworkMonitor::snapshotSingle(const std::string& /*interfaceName*/,
                                     InterfaceSnapshot& out) const
{
    out.nameUtf8     = m_interfaceName;
    out.bytesSent     = m_bytesSent.load();
    out.bytesReceived = m_bytesReceived.load();
    out.isUp          = m_running.load();
    return m_running.load();
}

// ============================================================
//  listInterfaces() — 列出所有 Npcap 可见网卡
// ============================================================
std::vector<std::string> NetworkMonitor::listInterfaces() const {
    std::vector<std::string> result;

    pcap_if_t* devs = nullptr;
    char errbuf[PCAP_ERRBUF_SIZE] = {};

    if (pcap_findalldevs(&devs, errbuf) != 0) {
        std::cerr << "[NetworkMonitor] pcap_findalldevs 失败: " << errbuf << "\n";
        return result;
    }

    for (pcap_if_t* d = devs; d != nullptr; d = d->next) {
        std::string entry = d->name;
        if (d->description) {
            entry += "  [" + std::string(d->description) + "]";
        }
        result.push_back(entry);
    }

    pcap_freealldevs(devs);
    return result;
}

// ============================================================
//  captureLoop() — 后台线程：持续获取并处理数据包
// ============================================================
void NetworkMonitor::captureLoop() {
    struct pcap_pkthdr* header = nullptr;
    const u_char*       data   = nullptr;

    while (!m_stopFlag.load()) {
        int ret = pcap_next_ex(m_handle, &header, &data);

        if (ret == 1) {
            // 成功捕获到一个包
            processPacket(data, static_cast<int>(header->caplen));
        } else if (ret == 0) {
            // 超时，继续循环
            continue;
        } else {
            // 错误或 pcap_breakloop 被调用
            break;
        }
    }

    m_running = false;
}

// ============================================================
//  processPacket() — 解析数据包，过滤 LAN 流量后计入统计
// ============================================================
void NetworkMonitor::processPacket(const u_char* data, int capLen) {
    // 最小长度：14（以太网头）+ 20（IP头）= 34 字节
    if (capLen < 34) return;

    // 解析以太网头
    const auto* eth = reinterpret_cast<const EtherHeader*>(data);

    // 只处理 IPv4（0x0800）
    if (ntohs(eth->etherType) != ETHERTYPE_IPV4) return;

    // 解析 IPv4 头
    const auto* ip = reinterpret_cast<const IPv4Header*>(data + sizeof(EtherHeader));

    uint32_t src = ip->srcIP;   // 网络字节序
    uint32_t dst = ip->dstIP;

    // ---- 局域网过滤 ----
    // 如果源和目标都是私有 IP，说明是纯 LAN 流量，直接跳过
    if (isPrivateIP(src) && isPrivateIP(dst)) return;

    // 数据包字节数（使用 IP 头中的总长度，不用 caplen）
    uint16_t ipTotalLen = ntohs(ip->totalLength);
    if (ipTotalLen == 0) return;

    // ---- 上传 / 下载 方向判断 ----
    // 源 IP 是本机 → 上传（发出）
    // 目标 IP 是本机 → 下载（接收）
    if (m_localIP != 0) {
        if (src == m_localIP) {
            m_bytesSent.fetch_add(ipTotalLen, std::memory_order_relaxed);
        } else if (dst == m_localIP) {
            m_bytesReceived.fetch_add(ipTotalLen, std::memory_order_relaxed);
        }
    } else {
        // 无法判断方向时，非私有 IP 的包均算下载
        if (!isPrivateIP(src)) {
            m_bytesReceived.fetch_add(ipTotalLen, std::memory_order_relaxed);
        }
        if (!isPrivateIP(dst)) {
            m_bytesSent.fetch_add(ipTotalLen, std::memory_order_relaxed);
        }
    }
}

// ============================================================
//  isPrivateIP() — 判断是否为私有/保留地址
//  参数：网络字节序的 uint32_t IP
// ============================================================
bool NetworkMonitor::isPrivateIP(uint32_t ipNetOrder) {
    uint32_t ip = ntohl(ipNetOrder);  // 转为主机字节序方便比较

    // 127.0.0.0/8 — 回环
    if ((ip & 0xFF000000) == 0x7F000000) return true;

    // 10.0.0.0/8 — A 类私有
    if ((ip & 0xFF000000) == 0x0A000000) return true;

    // 172.16.0.0/12 — B 类私有
    if ((ip & 0xFFF00000) == 0xAC100000) return true;

    // 192.168.0.0/16 — C 类私有
    if ((ip & 0xFFFF0000) == 0xC0A80000) return true;

    // 169.254.0.0/16 — 链路本地（APIPA）
    if ((ip & 0xFFFF0000) == 0xA9FE0000) return true;

    // 224.0.0.0/4 — 组播
    if ((ip & 0xF0000000) == 0xE0000000) return true;

    // 255.255.255.255 — 广播
    if (ip == 0xFFFFFFFF) return true;

    return false;
}

// ============================================================
//  getLocalIP() — 获取本机在该网卡上的 IP 地址
// ============================================================
uint32_t NetworkMonitor::getLocalIP(const std::string& pcapDevName) {
    // Npcap 设备名格式：\Device\NPF_{GUID}
    // 提取 GUID 部分来匹配 Windows 网络适配器
    std::string guid;
    auto pos = pcapDevName.find('{');
    if (pos != std::string::npos) {
        auto end = pcapDevName.find('}', pos);
        if (end != std::string::npos) {
            guid = pcapDevName.substr(pos, end - pos + 1);
        }
    }

    // 用 GetAdaptersAddresses 遍历所有网卡找到匹配的 IP
    ULONG bufLen = 15000;
    std::vector<BYTE> buf(bufLen);
    auto* pAddresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());

    if (GetAdaptersAddresses(AF_INET,
            GAA_FLAG_INCLUDE_PREFIX, nullptr,
            pAddresses, &bufLen) != NO_ERROR) {
        return 0;
    }

    for (auto* adapter = pAddresses;
         adapter != nullptr;
         adapter = adapter->Next)
    {
        // 将适配器名（GUID）转为字符串比较
        std::string adapterName = adapter->AdapterName;
        if (!guid.empty() &&
            adapterName.find(guid) == std::string::npos) continue;

        // 取第一个 IPv4 单播地址
        for (auto* ua = adapter->FirstUnicastAddress;
             ua != nullptr; ua = ua->Next)
        {
            if (ua->Address.lpSockaddr->sa_family == AF_INET) {
                auto* sin = reinterpret_cast<sockaddr_in*>(
                    ua->Address.lpSockaddr);
                return sin->sin_addr.s_addr;  // 网络字节序
            }
        }
    }

    return 0;
}

// ============================================================
//  autoSelectInterface() — 自动选择物理网卡
//  优先选择描述中含 Wi-Fi / Ethernet / LAN 的真实网卡
//  排除含 Virtual / VMware / Loopback / WAN Miniport 的虚拟网卡
// ============================================================
std::string NetworkMonitor::autoSelectInterface() const {
    pcap_if_t* devs = nullptr;
    char errbuf[PCAP_ERRBUF_SIZE] = {};

    if (pcap_findalldevs(&devs, errbuf) != 0) {
        std::cerr << "[NetworkMonitor] pcap_findalldevs 失败: " << errbuf << "\n";
        return {};
    }

    std::string selected;

    // 关键词黑名单（跳过这些虚拟/无用网卡）
    auto isVirtual = [](const std::string& desc) {
        std::string d = desc;
        std::transform(d.begin(), d.end(), d.begin(), ::tolower);
        return d.find("virtual")    != std::string::npos ||
               d.find("vmware")     != std::string::npos ||
               d.find("loopback")   != std::string::npos ||
               d.find("miniport")   != std::string::npos ||
               d.find("bluetooth")  != std::string::npos ||
               d.find("teredo")     != std::string::npos ||
               d.find("6to4")       != std::string::npos ||
               d.find("filter")     != std::string::npos ||
               d.find("scheduler")  != std::string::npos;
    };

    // 关键词白名单（优先选这些）
    auto isPreferred = [](const std::string& desc) {
        std::string d = desc;
        std::transform(d.begin(), d.end(), d.begin(), ::tolower);
        return d.find("wi-fi")    != std::string::npos ||
               d.find("wifi")     != std::string::npos ||
               d.find("wireless") != std::string::npos ||
               d.find("ethernet") != std::string::npos ||
               d.find("realtek")  != std::string::npos ||
               d.find("intel")    != std::string::npos ||
               d.find("mediatek") != std::string::npos;
    };

    for (pcap_if_t* d = devs; d != nullptr; d = d->next) {
        std::string desc = d->description ? d->description : "";
        if (isVirtual(desc)) continue;

        if (isPreferred(desc)) {
            selected = d->name;
            std::cout << "[NetworkMonitor] 自动选择网卡: " << desc << "\n";
            break;
        }

        // 备选：第一个非虚拟网卡
        if (selected.empty()) {
            selected = d->name;
        }
    }

    pcap_freealldevs(devs);
    return selected;
}

} // namespace NetGuard

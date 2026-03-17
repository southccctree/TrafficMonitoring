// ============================================================
//  NetworkMonitor.cpp  v2
//  修复：
//    1. immediate mode → 消除延迟
//    2. IPv6 解析     → 视频/QUIC 等 IPv6 流量不再漏统计
//    3. filterLan 开关 → VPN 网卡可复用此类
// ============================================================

#include "NetworkMonitor.h"

#include <iostream>
#include <algorithm>
#include <cstring>
#include <vector>
#include <array>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace NetGuard {

// ============================================================
//  以太网帧头 + IPv4 头 + IPv6 头（packed，防止编译器填充）
// ============================================================
#pragma pack(push, 1)
struct EtherHeader {
    uint8_t  dstMac[6];
    uint8_t  srcMac[6];
    uint16_t etherType;
};
struct IPv4Header {
    uint8_t  versionIHL;
    uint8_t  tos;
    uint16_t totalLength;
    uint16_t id;
    uint16_t flagsOffset;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t srcIP;
    uint32_t dstIP;
};
struct IPv6Header {
    uint32_t versionTcFlow;   // version(4) + traffic class(8) + flow label(20)
    uint16_t payloadLength;   // 不含 40 字节固定头
    uint8_t  nextHeader;
    uint8_t  hopLimit;
    uint8_t  srcIP[16];
    uint8_t  dstIP[16];
};
#pragma pack(pop)

static constexpr uint16_t ETHERTYPE_IPV4 = 0x0800;
static constexpr uint16_t ETHERTYPE_IPV6 = 0x86DD;
static constexpr int      ETH_HLEN       = 14;
static constexpr int      IPV6_HLEN      = 40;

// ============================================================
//  start() — 使用 immediate mode 打开网卡
// ============================================================
bool NetworkMonitor::start(const std::string& interfaceName, bool filterLan) {
    if (m_running.load()) return true;

    m_filterLan = filterLan;

    std::string devName = (interfaceName == "auto")
        ? autoSelectInterface()
        : interfaceName;

    if (devName.empty()) {
        std::cerr << "[NetworkMonitor] 未找到合适的网卡\n";
        return false;
    }

    m_localIPv4 = getLocalIPv4(devName);
    m_localIPv6Addrs = getLocalIPv6List(devName);

    // ---- 使用 pcap_create + immediate mode 替代 pcap_open_live ----
    char errbuf[PCAP_ERRBUF_SIZE] = {};
    m_handle = pcap_create(devName.c_str(), errbuf);
    if (!m_handle) {
        std::cerr << "[NetworkMonitor] pcap_create 失败: " << errbuf << "\n";
        return false;
    }

    pcap_set_snaplen(m_handle, 65535);
    pcap_set_promisc(m_handle, 0);
    pcap_set_timeout(m_handle, 100);

    // 关键：立即模式，数据包到达即上报，不缓冲
    if (pcap_set_immediate_mode(m_handle, 1) != 0) {
        std::cerr << "[NetworkMonitor] 警告：immediate mode 设置失败，"
                     "延迟可能较高\n";
    }

    if (pcap_activate(m_handle) != 0) {
        std::cerr << "[NetworkMonitor] pcap_activate 失败: "
                  << pcap_geterr(m_handle) << "\n";
        pcap_close(m_handle);
        m_handle = nullptr;
        return false;
    }

    // 只捕获 IPv4 和 IPv6
    struct bpf_program fp;
    if (pcap_compile(m_handle, &fp, "ip or ip6",
                     0, PCAP_NETMASK_UNKNOWN) == 0) {
        pcap_setfilter(m_handle, &fp);
        pcap_freecode(&fp);
    } else {
        std::cerr << "[NetworkMonitor] 警告：BPF 过滤器设置失败: "
                  << pcap_geterr(m_handle) << "\n";
    }

    m_interfaceName = devName;
    m_stopFlag      = false;
    m_running       = true;

    std::cout << "[NetworkMonitor] 已启动（"
              << (filterLan ? "公网过滤" : "全量统计")
              << "，immediate mode）: " << devName << "\n";

    if (m_localIPv4 != 0) {
        in_addr addr;
        addr.s_addr = m_localIPv4;
        std::cout << "[NetworkMonitor] 本机 IPv4: "
                  << inet_ntoa(addr) << "\n";
    }
    if (!m_localIPv6Addrs.empty()) {
        std::cout << "[NetworkMonitor] 本机 IPv6 地址数量: "
                  << m_localIPv6Addrs.size() << "\n";
    }

    m_thread = std::thread([this]() { captureLoop(); });
    return true;
}

// ============================================================
//  stop()
// ============================================================
void NetworkMonitor::stop() {
    if (!m_running.load()) return;
    m_stopFlag = true;
    if (m_handle) pcap_breakloop(m_handle);
    if (m_thread.joinable()) m_thread.join();
    if (m_handle) { pcap_close(m_handle); m_handle = nullptr; }
    m_running = false;
}

// ============================================================
//  snapshotSingle()
// ============================================================
bool NetworkMonitor::snapshotSingle(const std::string&,
                                     InterfaceSnapshot& out) const
{
    out.nameUtf8      = m_interfaceName;
    out.bytesSent     = m_bytesSent.load();
    out.bytesReceived = m_bytesReceived.load();
    out.isUp          = m_running.load();
    return m_running.load();
}

// ============================================================
//  listInterfaces()
// ============================================================
std::vector<std::string> NetworkMonitor::listInterfaces() const {
    std::vector<std::string> result;
    pcap_if_t* devs = nullptr;
    char errbuf[PCAP_ERRBUF_SIZE] = {};
    if (pcap_findalldevs(&devs, errbuf) != 0) return result;
    for (pcap_if_t* d = devs; d != nullptr; d = d->next) {
        std::string entry = d->name;
        if (d->description)
            entry += "  [" + std::string(d->description) + "]";
        result.push_back(entry);
    }
    pcap_freealldevs(devs);
    return result;
}

// ============================================================
//  captureLoop()
// ============================================================
void NetworkMonitor::captureLoop() {
    struct pcap_pkthdr* header = nullptr;
    const u_char*       data   = nullptr;

    while (!m_stopFlag.load()) {
        int ret = pcap_next_ex(m_handle, &header, &data);
        if      (ret == 1)  processPacket(data, (int)header->caplen);
        else if (ret == 0)  continue;   // timeout
        else                break;      // error / breakloop
    }
    m_running = false;
}

// ============================================================
//  processPacket() — 同时处理 IPv4 和 IPv6
// ============================================================
void NetworkMonitor::processPacket(const u_char* data, int capLen) {
    if (capLen < ETH_HLEN) return;

    const auto* eth      = reinterpret_cast<const EtherHeader*>(data);
    uint16_t    ethType  = ntohs(eth->etherType);

    // ---- IPv4 ----
    if (ethType == ETHERTYPE_IPV4) {
        if (capLen < ETH_HLEN + 20) return;
        const auto* ip = reinterpret_cast<const IPv4Header*>(data + ETH_HLEN);

        uint32_t src = ip->srcIP;
        uint32_t dst = ip->dstIP;

        // LAN 过滤：源和目标都是私有地址则跳过
        if (m_filterLan && isPrivateIPv4(src) && isPrivateIPv4(dst)) return;

        uint16_t len = ntohs(ip->totalLength);
        if (len == 0) return;

        if (m_localIPv4 != 0) {
            if      (src == m_localIPv4) m_bytesSent.fetch_add(len,     std::memory_order_relaxed);
            else if (dst == m_localIPv4) m_bytesReceived.fetch_add(len, std::memory_order_relaxed);
        } else {
            if (!isPrivateIPv4(src)) m_bytesSent.fetch_add(len,     std::memory_order_relaxed);
            if (!isPrivateIPv4(dst)) m_bytesReceived.fetch_add(len, std::memory_order_relaxed);
        }
        return;
    }

    // ---- IPv6 ----
    if (ethType == ETHERTYPE_IPV6) {
        if (capLen < ETH_HLEN + IPV6_HLEN) return;
        const auto* ip6 = reinterpret_cast<const IPv6Header*>(data + ETH_HLEN);

        bool srcPrivate = isPrivateIPv6(ip6->srcIP);
        bool dstPrivate = isPrivateIPv6(ip6->dstIP);

        if (m_filterLan && srcPrivate && dstPrivate) return;

        // IPv6 长度 = 固定头 40 + payloadLength
        uint16_t payLen = ntohs(ip6->payloadLength);
        uint32_t totalLen = IPV6_HLEN + payLen;
        if (totalLen == 0) return;

        auto isLocalIPv6 = [this](const uint8_t ip[16]) {
            for (const auto& local : m_localIPv6Addrs) {
                if (memcmp(local.data(), ip, 16) == 0) {
                    return true;
                }
            }
            return false;
        };

        bool srcLocal = isLocalIPv6(ip6->srcIP);
        bool dstLocal = isLocalIPv6(ip6->dstIP);

        if (srcLocal && !dstLocal) {
            m_bytesSent.fetch_add(totalLen, std::memory_order_relaxed);
        } else if (dstLocal && !srcLocal) {
            m_bytesReceived.fetch_add(totalLen, std::memory_order_relaxed);
        } else if (!srcLocal && !dstLocal) {
            // 无法判定方向时，只记一侧，避免同包双计数。
            m_bytesReceived.fetch_add(totalLen, std::memory_order_relaxed);
        }
    }
}

// ============================================================
//  isPrivateIPv4()
// ============================================================
bool NetworkMonitor::isPrivateIPv4(uint32_t ipNetOrder) {
    uint32_t ip = ntohl(ipNetOrder);
    if ((ip & 0xFF000000) == 0x7F000000) return true;  // 127.x
    if ((ip & 0xFF000000) == 0x0A000000) return true;  // 10.x
    if ((ip & 0xFFF00000) == 0xAC100000) return true;  // 172.16-31.x
    if ((ip & 0xFFFF0000) == 0xC0A80000) return true;  // 192.168.x
    if ((ip & 0xFFFF0000) == 0xA9FE0000) return true;  // 169.254.x
    if ((ip & 0xF0000000) == 0xE0000000) return true;  // 组播 224.x
    if (ip == 0xFFFFFFFF)                return true;  // 广播
    return false;
}

// ============================================================
//  isPrivateIPv6()
// ============================================================
bool NetworkMonitor::isPrivateIPv6(const uint8_t ip[16]) {
    // ::1 回环
    static const uint8_t loopback[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
    if (memcmp(ip, loopback, 16) == 0) return true;
    // fc00::/7 唯一本地
    if ((ip[0] & 0xFE) == 0xFC) return true;
    // fe80::/10 链路本地
    if (ip[0] == 0xFE && (ip[1] & 0xC0) == 0x80) return true;
    // ff00::/8 组播
    if (ip[0] == 0xFF) return true;
    return false;
}

// ============================================================
//  getLocalIPv4()
// ============================================================
uint32_t NetworkMonitor::getLocalIPv4(const std::string& pcapDevName) {
    std::string guid;
    auto pos = pcapDevName.find('{');
    if (pos != std::string::npos) {
        auto end = pcapDevName.find('}', pos);
        if (end != std::string::npos)
            guid = pcapDevName.substr(pos, end - pos + 1);
    }

    ULONG bufLen = 15000;
    std::vector<BYTE> buf(bufLen);
    auto* pAddr = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());

    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX,
                             nullptr, pAddr, &bufLen) != NO_ERROR)
        return 0;

    for (auto* a = pAddr; a != nullptr; a = a->Next) {
        if (!guid.empty() &&
            std::string(a->AdapterName).find(guid) == std::string::npos)
            continue;
        for (auto* ua = a->FirstUnicastAddress; ua; ua = ua->Next) {
            if (ua->Address.lpSockaddr->sa_family == AF_INET) {
                auto* sin = reinterpret_cast<sockaddr_in*>(
                    ua->Address.lpSockaddr);
                return sin->sin_addr.s_addr;
            }
        }
    }
    return 0;
}

// ============================================================
//  getLocalIPv6List()
// ============================================================
std::vector<std::array<uint8_t, 16>>
NetworkMonitor::getLocalIPv6List(const std::string& pcapDevName) {
    std::vector<std::array<uint8_t, 16>> addrs;

    std::string guid;
    auto pos = pcapDevName.find('{');
    if (pos != std::string::npos) {
        auto end = pcapDevName.find('}', pos);
        if (end != std::string::npos)
            guid = pcapDevName.substr(pos, end - pos + 1);
    }

    ULONG bufLen = 15000;
    std::vector<BYTE> buf(bufLen);
    auto* pAddr = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());

    if (GetAdaptersAddresses(AF_INET6, GAA_FLAG_INCLUDE_PREFIX,
                             nullptr, pAddr, &bufLen) != NO_ERROR)
        return addrs;

    for (auto* a = pAddr; a != nullptr; a = a->Next) {
        if (!guid.empty() &&
            std::string(a->AdapterName).find(guid) == std::string::npos)
            continue;

        for (auto* ua = a->FirstUnicastAddress; ua; ua = ua->Next) {
            if (ua->Address.lpSockaddr->sa_family == AF_INET6) {
                auto* sin6 = reinterpret_cast<sockaddr_in6*>(
                    ua->Address.lpSockaddr);
                std::array<uint8_t, 16> ip{};
                memcpy(ip.data(), sin6->sin6_addr.s6_addr, 16);
                addrs.push_back(ip);
            }
        }
    }

    return addrs;
}

// ============================================================
//  autoSelectInterface()
// ============================================================
std::string NetworkMonitor::autoSelectInterface() const {
    pcap_if_t* devs = nullptr;
    char errbuf[PCAP_ERRBUF_SIZE] = {};
    if (pcap_findalldevs(&devs, errbuf) != 0) return {};

    auto isVirtual = [](const std::string& d) {
        std::string s = d;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s.find("virtual")   != std::string::npos ||
               s.find("vmware")    != std::string::npos ||
               s.find("loopback")  != std::string::npos ||
               s.find("miniport")  != std::string::npos ||
               s.find("bluetooth") != std::string::npos ||
               s.find("teredo")    != std::string::npos ||
               s.find("6to4")      != std::string::npos ||
               s.find("filter")    != std::string::npos ||
               s.find("scheduler") != std::string::npos;
    };

    auto isPreferred = [](const std::string& d) {
        std::string s = d;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s.find("wi-fi")    != std::string::npos ||
               s.find("wifi")     != std::string::npos ||
               s.find("wireless") != std::string::npos ||
               s.find("ethernet") != std::string::npos ||
               s.find("realtek")  != std::string::npos ||
               s.find("intel")    != std::string::npos ||
               s.find("mediatek") != std::string::npos;
    };

    std::string selected, fallback;
    for (pcap_if_t* d = devs; d != nullptr; d = d->next) {
        std::string desc = d->description ? d->description : "";
        if (isVirtual(desc)) continue;
        if (isPreferred(desc) && selected.empty()) {
            selected = d->name;
            std::cout << "[NetworkMonitor] 自动选择: " << desc << "\n";
        }
        if (fallback.empty()) fallback = d->name;
    }

    pcap_freealldevs(devs);
    return selected.empty() ? fallback : selected;
}

} // namespace NetGuard

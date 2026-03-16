// ============================================================
//  NetworkMonitor.cpp
//  职责：实现网卡数据采集逻辑
//  依赖：iphlpapi（Windows SDK 自带，CMake 中需链接）
// ============================================================
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600 // Windows Vista 或更高版本
#endif
#include "NetworkMonitor.h"

#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <netioapi.h>   // MIB_IF_TABLE2, MIB_IF_ROW2, GetIfTable2, FreeMibTable

// 链接 iphlpapi 库（也可在 CMakeLists.txt 中配置）
#pragma comment(lib, "iphlpapi.lib")

namespace NetGuard {

// ============================================================
//  snapshot() — 采集所有网卡的当前字节快照
// ============================================================
bool NetworkMonitor::snapshot(std::vector<InterfaceSnapshot>& out) const {
    out.clear();

    // GetIfTable2 返回一个动态分配的 MIB_IF_TABLE2 结构
    // 必须用 FreeMibTable() 释放，这里用 RAII 包装
    MIB_IF_TABLE2* pTable = nullptr;
    DWORD result = GetIfTable2(&pTable);

    if (result != NO_ERROR) {
        std::cerr << "[NetworkMonitor] GetIfTable2 失败，错误码: " << result << "\n";
        return false;
    }

    // RAII：离开作用域时自动释放
    struct TableGuard {
        MIB_IF_TABLE2* ptr;
        ~TableGuard() { if (ptr) FreeMibTable(ptr); }
    } guard{ pTable };

    out.reserve(pTable->NumEntries);

    for (ULONG i = 0; i < pTable->NumEntries; ++i) {
        const MIB_IF_ROW2& row = pTable->Table[i];

        // 跳过回环网卡（lo）和未连接状态的虚拟网卡
        if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK) continue;

        InterfaceSnapshot snap;
        snap.name         = row.Description;              // 宽字符描述名
        snap.nameUtf8     = wideToUtf8(row.Description);
        snap.bytesSent     = row.OutOctets;               // 累计发送字节
        snap.bytesReceived = row.InOctets;                // 累计接收字节
        snap.isUp          = (row.OperStatus == IfOperStatusUp);

        out.push_back(std::move(snap));
    }

    return true;
}

// ============================================================
//  snapshotSingle() — 返回指定网卡的单条快照
// ============================================================
bool NetworkMonitor::snapshotSingle(const std::string& interfaceName,
                                    InterfaceSnapshot& out) const {
    std::vector<InterfaceSnapshot> all;
    if (!snapshot(all) || all.empty()) return false;

    // "auto" 模式：选择累计字节数最大的网卡
    if (interfaceName == "auto") {
        const InterfaceSnapshot* best = findBusiest(all);
        if (!best) return false;
        out = *best;
        return true;
    }

    // 按名称匹配（大小写不敏感）
    std::string target = interfaceName;
    std::transform(target.begin(), target.end(), target.begin(), ::tolower);

    for (const auto& snap : all) {
        std::string name = snap.nameUtf8;
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        if (name.find(target) != std::string::npos) {
            out = snap;
            return true;
        }
    }

    std::cerr << "[NetworkMonitor] 未找到网卡: " << interfaceName << "\n";
    return false;
}

// ============================================================
//  listInterfaces() — 返回所有网卡名称列表
// ============================================================
std::vector<std::string> NetworkMonitor::listInterfaces() const {
    std::vector<InterfaceSnapshot> all;
    std::vector<std::string> names;

    if (!snapshot(all)) return names;

    for (const auto& snap : all) {
        names.push_back(snap.nameUtf8
            + (snap.isUp ? " [已连接]" : " [未连接]"));
    }
    return names;
}

// ============================================================
//  wideToUtf8() — 宽字符转 UTF-8
// ============================================================
std::string NetworkMonitor::wideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};

    // 计算需要的缓冲区大小
    int size = WideCharToMultiByte(
        CP_UTF8, 0,
        wide.c_str(), static_cast<int>(wide.size()),
        nullptr, 0,
        nullptr, nullptr);

    if (size <= 0) return {};

    std::string result(size, '\0');
    WideCharToMultiByte(
        CP_UTF8, 0,
        wide.c_str(), static_cast<int>(wide.size()),
        result.data(), size,
        nullptr, nullptr);

    return result;
}

// ============================================================
//  findBusiest() — 找出累计流量最大的活跃网卡
// ============================================================
const InterfaceSnapshot* NetworkMonitor::findBusiest(
    const std::vector<InterfaceSnapshot>& list)
{
    const InterfaceSnapshot* best = nullptr;
    uint64_t maxBytes = 0;

    for (const auto& snap : list) {
        // 优先选择已连接的网卡
        if (!snap.isUp) continue;

        uint64_t total = snap.bytesSent + snap.bytesReceived;
        if (total > maxBytes) {
            maxBytes = total;
            best = &snap;
        }
    }

    // 若没有已连接的网卡，退而选择任意流量最大的
    if (!best && !list.empty()) {
        for (const auto& snap : list) {
            uint64_t total = snap.bytesSent + snap.bytesReceived;
            if (total > maxBytes) {
                maxBytes = total;
                best = &snap;
            }
        }
    }

    return best;
}

} // namespace NetGuard

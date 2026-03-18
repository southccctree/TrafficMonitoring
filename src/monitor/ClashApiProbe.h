#pragma once

// ============================================================
//  ClashApiProbe.h
//  职责：通过 Clash 外部控制 API 的 /traffic 接口
//        获取每秒实时流量数据
//  实现：WinHTTP 长连接，独立后台线程持续读取 SSE 推流
//  线程安全：通过原子变量向主线程暴露最新的每秒速率
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#pragma comment(lib, "winhttp.lib")

namespace NetGuard {

class ClashApiProbe {
public:
    ClashApiProbe() = default;
    ~ClashApiProbe() { stop(); }

    // 从配置文件解析 API 地址和密钥并启动连接
    // configPath: Clash 配置文件路径，如 "C:/Users/xxx/.config/clash/config.yaml"
    // 若解析失败或连接失败，静默返回 false，不崩溃
    bool start(const std::string& configPath);

    // 使用显式参数启动（不读配置文件）
    bool start(const std::string& host, uint16_t port,
               const std::string& secret);

    void stop();

    // 是否已连接到 Clash API
    bool isOnline() const { return m_online.load(); }

    // 最近一次推送的每秒上传字节数
    uint64_t lastUpBytes() const { return m_lastUp.load(); }

    // 最近一次推送的每秒下载字节数
    uint64_t lastDownBytes() const { return m_lastDown.load(); }

    // 尝试重连（主循环每 30 秒调用一次）
    // 若已在线直接返回 true；若离线则重新解析配置并重连
    bool tryReconnect();

private:
    // 后台线程：维持 WinHTTP 长连接，持续解析 SSE 数据
    void sseLoop();

    // 解析 Clash config.yaml，提取 external-controller 和 secret
    // 使用简单字符串查找，不引入 YAML 解析库
    static bool parseClashConfig(const std::string& configPath,
                                 std::string& outHost,
                                 uint16_t& outPort,
                                 std::string& outSecret);

    // 解析单行 SSE data，提取 up/down 数值
    // 输入: 'data: {"up":1234,"down":5678}'
    // 输出: up=1234, down=5678，返回 false 表示解析失败
    static bool parseSseLine(const std::string& line,
                             uint64_t& up, uint64_t& down);

    std::string m_configPath;
    std::string m_host;
    uint16_t m_port = 0;
    std::string m_secret;

    std::atomic<bool> m_online{ false };
    std::atomic<bool> m_stopFlag{ false };
    std::atomic<uint64_t> m_lastUp{ 0 };
    std::atomic<uint64_t> m_lastDown{ 0 };

    std::thread m_thread;
};

} // namespace NetGuard

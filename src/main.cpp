// ============================================================
//  main.cpp  v2
//  新增：
//    - 第二个 NetworkMonitor 实例专门监控 VPN 网卡
//    - VpnTracker（复用 SessionTracker）独立累计 VPN 用量
//    - Notifier 冷却时间从配置读取
// ============================================================

#include "config/Config.h"
#include "monitor/NetworkMonitor.h"
#include "monitor/SpeedCalculator.h"
#include "stats/DailyTracker.h"
#include "stats/SessionTracker.h"
#include "alert/AlertManager.h"
#include "alert/Notifier.h"
#include "ui/OverlayWindow.h"
#include "ui/Renderer.h"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>

static std::atomic<bool> g_running{ true };
void onSignal(int) { g_running = false; }

int main() {
    SetConsoleOutputCP(CP_UTF8);

    std::cout << "========================================\n";
    std::cout << "  NetGuard — 网络流量监控工具 启动中...\n";
    std::cout << "========================================\n";

    std::signal(SIGINT,  onSignal);
    std::signal(SIGTERM, onSignal);

    // --------------------------------------------------------
    //  1. 加载配置
    // --------------------------------------------------------
    NetGuard::Config& cfg = NetGuard::Config::instance();
    cfg.load("config/netguard.json");
    const auto& appCfg = cfg.get();

    std::cout << "[main] 每日限额: "    << appCfg.alert.dailyLimitMB        << " MB\n";
    std::cout << "[main] VPN 限额: "    << appCfg.alert.vpnLimitMB          << " MB\n";
    std::cout << "[main] 弹窗冷却: "    << appCfg.alert.notifyCooldownSec   << " 秒\n";
    std::cout << "[main] 主网卡: "      << appCfg.monitor.networkInterface   << "\n";
    std::cout << "[main] VPN 网卡: "    << (appCfg.monitor.vpnInterface.empty()
                                            ? "（未配置，VPN 用量将复用主网卡数据）"
                                            : appCfg.monitor.vpnInterface)   << "\n";

    // --------------------------------------------------------
    //  2. 初始化模块
    // --------------------------------------------------------
    NetGuard::NetworkMonitor  networkMonitor;     // 主网卡（公网过滤）
    NetGuard::NetworkMonitor  vpnMonitor;         // VPN 网卡（全量统计）
    NetGuard::SpeedCalculator speedCalc;
    NetGuard::SpeedCalculator vpnSpeedCalc;
    NetGuard::DailyTracker    dailyTracker;
    NetGuard::DailyTracker    vpnDailyTracker;    // VPN 独立每日统计
    NetGuard::SessionTracker  sessionTracker;
    NetGuard::AlertManager    alertManager;
    NetGuard::Notifier        notifier;
    NetGuard::OverlayWindow   overlayWindow;
    NetGuard::Renderer        renderer;

    // 打印可用网卡列表
    auto interfaces = networkMonitor.listInterfaces();
    std::cout << "[main] 检测到网卡列表:\n";
    for (const auto& iface : interfaces)
        std::cout << "         - " << iface << "\n";

    // ---- 启动主网卡监控（过滤 LAN）----
    if (!networkMonitor.start(appCfg.monitor.networkInterface, true)) {
        std::cerr << "[main] 主网卡 Npcap 启动失败，请确认已安装 Npcap 并以管理员身份运行\n";
        return 1;
    }

    // ---- 启动 VPN 网卡监控（不过滤 LAN，统计全量）----
    bool vpnMonitorOk = false;
    if (!appCfg.monitor.vpnInterface.empty()) {
        vpnMonitorOk = vpnMonitor.start(appCfg.monitor.vpnInterface, false);
        if (!vpnMonitorOk) {
            std::cerr << "[main] VPN 网卡启动失败，VPN 用量将复用主网卡数据\n";
        }
    }

    // ---- 加载每日数据 ----
    dailyTracker.load("data/daily_usage.json");
    if (vpnMonitorOk)
        vpnDailyTracker.load("data/vpn_usage.json");

    sessionTracker.start();

    // --------------------------------------------------------
    //  3. 创建窗口
    // --------------------------------------------------------
    bool windowOk = overlayWindow.create(
        appCfg.window.positionX, appCfg.window.positionY,
        appCfg.window.width,     appCfg.window.height,
        static_cast<BYTE>(appCfg.window.opacity));

    if (!windowOk)
        std::cerr << "[main] 置顶窗口创建失败，以控制台模式运行\n";

    // --------------------------------------------------------
    //  4. 初始化渲染器
    // --------------------------------------------------------
    if (!renderer.init())
        std::cerr << "[main] 渲染器初始化失败\n";

    if (windowOk) {
        overlayWindow.setPaintCallback(
            [&renderer](HDC hdc, RECT rc) { renderer.draw(hdc, rc); });
    }

    // --------------------------------------------------------
    //  5. 初始化通知器
    // --------------------------------------------------------
    if (windowOk)
        notifier.init(overlayWindow.hwnd());

    // 从配置读取冷却时间
    notifier.setCooldown(appCfg.alert.notifyCooldownSec);
    notifier.setReminderInterval(appCfg.alert.notifyCooldownSec);

    notifier.setColorCallback(
        [&overlayWindow](NetGuard::WindowColorSignal sig) {
            if (overlayWindow.isRunning())
                overlayWindow.setColor(sig);
        });

    // --------------------------------------------------------
    //  6. 主循环
    // --------------------------------------------------------
    std::cout << "[main] 开始监控，按 Ctrl+C 退出\n";
    std::cout << "----------------------------------------\n";

    auto lastSaveTime = std::chrono::steady_clock::now();
    constexpr int SAVE_INTERVAL_SEC = 10;
    int consoleTick = 0;

    while (g_running.load() && (!windowOk || overlayWindow.isRunning())) {

        // -- 6.1 主网卡快照 → 速度 → 累计 --
        NetGuard::InterfaceSnapshot snap;
        if (networkMonitor.snapshotSingle(appCfg.monitor.networkInterface, snap)) {
            speedCalc.update(snap);
            if (speedCalc.isReady()) {
                dailyTracker.addBytes(speedCalc.deltaUpload(),
                                      speedCalc.deltaDownload());
                dailyTracker.checkRollover();
                sessionTracker.addBytes(speedCalc.deltaUpload(),
                                        speedCalc.deltaDownload());
            }
        }

        // -- 6.2 VPN 网卡快照 → VPN 每日累计 --
        double vpnUsedMB = 0.0;
        if (vpnMonitorOk) {
            NetGuard::InterfaceSnapshot vpnSnap;
            if (vpnMonitor.snapshotSingle(appCfg.monitor.vpnInterface, vpnSnap)) {
                vpnSpeedCalc.update(vpnSnap);
                if (vpnSpeedCalc.isReady()) {
                    vpnDailyTracker.addBytes(vpnSpeedCalc.deltaUpload(),
                                             vpnSpeedCalc.deltaDownload());
                    vpnDailyTracker.checkRollover();
                }
            }
            vpnUsedMB = vpnDailyTracker.record().totalMB();
        } else {
            // VPN 网卡未配置或启动失败：视为未启用
            vpnUsedMB = 0.0;
        }

        // -- 6.3 评估警报 --
        const auto& daily   = dailyTracker.record();
        const auto  session = sessionTracker.snapshot();
        const auto& speed   = speedCalc.latest();

        NetGuard::AlertStatus alertStatus = alertManager.evaluate(
            daily.totalMB(), vpnUsedMB,
            speed.uploadKBps(), speed.downloadKBps());

        // -- 6.4 通知 --
        notifier.notify(alertStatus);

        // -- 6.5 渲染 --
        // 将 vpnUsedMB 注入 alertStatus 供 Renderer 使用
        renderer.update(speed, daily, session, alertStatus);
        if (windowOk) overlayWindow.requestRedraw();

        // -- 6.6 控制台输出（每 5 秒一次）--
        if (++consoleTick >= 5) {
            consoleTick = 0;
            std::cout << "\r[" << daily.date << "]"
                      << "  ▲ " << speed.uploadFormatted()
                      << "  ▼ " << speed.downloadFormatted()
                      << "  今日: " << std::fixed << std::setprecision(1)
                      << daily.totalMB() << " MB";
            if (vpnMonitorOk)
                std::cout << "  VPN: " << std::fixed << std::setprecision(1)
                          << vpnUsedMB << " MB";
            std::cout << "  " << alertStatus.levelDescription()
                      << "          " << std::flush;
        }

        // -- 6.7 定期持久化 --
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(now - lastSaveTime).count()
                >= SAVE_INTERVAL_SEC) {
            dailyTracker.save();
            if (vpnMonitorOk) vpnDailyTracker.save();
            lastSaveTime = now;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(appCfg.monitor.refreshIntervalMs));
    }

    // --------------------------------------------------------
    //  7. 清理退出
    // --------------------------------------------------------
    std::cout << "\n[main] 正在退出，保存数据...\n";
    dailyTracker.save();
    if (vpnMonitorOk) vpnDailyTracker.save();
    notifier.shutdown();
    overlayWindow.destroy();
    renderer.shutdown();
    std::cout << "[main] NetGuard 已退出\n";
    return 0;
}

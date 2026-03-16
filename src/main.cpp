// ============================================================
//  main.cpp
//  职责：程序入口，负责初始化所有模块，驱动主循环
//
//  主循环逻辑（每 refreshIntervalMs 毫秒执行一次）：
//    1. NetworkMonitor  采集网卡字节快照
//    2. SpeedCalculator 计算实时速度和字节增量
//    3. DailyTracker    累计今日流量，检查跨日归零
//    4. SessionTracker  累计会话流量
//    5. AlertManager    评估警报状态
//    6. Notifier        执行通知动作
//    7. Renderer        更新绘制数据
//    8. OverlayWindow   触发窗口重绘
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
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>

// ============================================================
//  全局退出标志（Ctrl+C 或窗口关闭时设置）
// ============================================================
static std::atomic<bool> g_running{ true };

void onSignal(int) {
    g_running = false;
}

// ============================================================
//  main()
// ============================================================
int main() {
    // 设置控制台编码为 UTF-8（中文日志正确显示）
    SetConsoleOutputCP(CP_UTF8);

    std::cout << "========================================\n";
    std::cout << "  NetGuard — 网络流量监控工具 启动中...\n";
    std::cout << "========================================\n";

    // 注册 Ctrl+C 信号处理
    std::signal(SIGINT,  onSignal);
    std::signal(SIGTERM, onSignal);

    // --------------------------------------------------------
    //  1. 加载配置
    // --------------------------------------------------------
    NetGuard::Config& cfg = NetGuard::Config::instance();
    cfg.load("config/netguard.json");

    const auto& appCfg = cfg.get();

    std::cout << "[main] 每日限额: "   << appCfg.alert.dailyLimitMB << " MB\n";
    std::cout << "[main] VPN 限额: "   << appCfg.alert.vpnLimitMB   << " MB\n";
    std::cout << "[main] 刷新间隔: "   << appCfg.monitor.refreshIntervalMs << " ms\n";
    std::cout << "[main] 监控网卡: "   << appCfg.monitor.networkInterface  << "\n";

    // --------------------------------------------------------
    //  2. 初始化各功能模块
    // --------------------------------------------------------
    NetGuard::NetworkMonitor  networkMonitor;
    NetGuard::SpeedCalculator speedCalc;
    NetGuard::DailyTracker    dailyTracker;
    NetGuard::SessionTracker  sessionTracker;
    NetGuard::AlertManager    alertManager;
    NetGuard::Notifier        notifier;
    NetGuard::OverlayWindow   overlayWindow;
    NetGuard::Renderer        renderer;

    // 打印所有可用网卡（方便用户配置时选择）
    auto interfaces = networkMonitor.listInterfaces();
    std::cout << "[main] 检测到网卡列表:\n";
    for (const auto& iface : interfaces) {
        std::cout << "         - " << iface << "\n";
    }

    // 加载今日流量数据
    dailyTracker.load("data/daily_usage.json");

    // 启动会话追踪
    sessionTracker.start();

    // --------------------------------------------------------
    //  3. 创建置顶小窗口
    // --------------------------------------------------------
    bool windowOk = overlayWindow.create(
        appCfg.window.positionX,
        appCfg.window.positionY,
        appCfg.window.width,
        appCfg.window.height,
        static_cast<BYTE>(appCfg.window.opacity)
    );

    if (!windowOk) {
        std::cerr << "[main] 置顶窗口创建失败，程序将以控制台模式运行\n";
    }

    // --------------------------------------------------------
    //  4. 初始化渲染器
    // --------------------------------------------------------
    if (!renderer.init()) {
        std::cerr << "[main] 渲染器初始化失败\n";
    }

    // 将渲染器的 draw() 注册为窗口绘制回调
    if (windowOk) {
        overlayWindow.setPaintCallback(
            [&renderer](HDC hdc, RECT rc) {
                renderer.draw(hdc, rc);
            }
        );
    }

    // --------------------------------------------------------
    //  5. 初始化通知器（需要窗口句柄）
    // --------------------------------------------------------
    if (windowOk) {
        notifier.init(overlayWindow.hwnd());
    }

    // 注册窗口变色回调：警报等级变化时通知窗口更新背景色
    notifier.setColorCallback(
        [&overlayWindow](NetGuard::WindowColorSignal sig) {
            if (overlayWindow.isRunning()) {
                overlayWindow.setColor(sig);
            }
        }
    );

    // --------------------------------------------------------
    //  6. 主循环
    // --------------------------------------------------------
    std::cout << "[main] 开始监控，按 Ctrl+C 退出\n";
    std::cout << "----------------------------------------\n";

    // 持久化计时器：每 10 秒保存一次数据，避免频繁写盘
    auto lastSaveTime = std::chrono::steady_clock::now();
    constexpr int SAVE_INTERVAL_SEC = 10;

    while (g_running.load() && overlayWindow.isRunning()) {

        // -- 6.1 采集网卡快照 --
        NetGuard::InterfaceSnapshot snap;
        bool snapOk = networkMonitor.snapshotSingle(
            appCfg.monitor.networkInterface, snap);

        if (snapOk) {
            // -- 6.2 计算速度和字节增量 --
            speedCalc.update(snap);

            if (speedCalc.isReady()) {
                uint64_t deltaUp   = speedCalc.deltaUpload();
                uint64_t deltaDown = speedCalc.deltaDownload();

                // -- 6.3 累计今日流量 --
                dailyTracker.addBytes(deltaUp, deltaDown);
                dailyTracker.checkRollover(); // 检查是否跨日

                // -- 6.4 累计会话流量 --
                sessionTracker.addBytes(deltaUp, deltaDown);
            }
        }

        // -- 6.5 评估警报状态 --
        const auto& daily   = dailyTracker.record();
        const auto  session = sessionTracker.snapshot();
        const auto& speed   = speedCalc.latest();

        NetGuard::AlertStatus alertStatus = alertManager.evaluate(
            daily.totalMB(),
            daily.totalMB(),             // VPN 用量与今日用量相同
            speed.uploadKBps(),
            speed.downloadKBps()
        );

        // -- 6.6 执行通知动作 --
        notifier.notify(alertStatus);

        // -- 6.7 更新渲染数据并触发重绘 --
        renderer.update(speed, daily, session, alertStatus);
        if (windowOk) {
            overlayWindow.requestRedraw();
        }

        // -- 6.8 控制台简要输出（每 5 秒打印一次，减少刷屏）--
        static int consoleTick = 0;
        if (++consoleTick >= 5) {
            consoleTick = 0;
            std::cout << "\r[" << daily.date << "]"
                      << "  ▲ " << speed.uploadFormatted()
                      << "  ▼ " << speed.downloadFormatted()
                      << "  今日: " << std::fixed
                      << std::setprecision(1) << daily.totalMB() << " MB"
                      << "  " << alertStatus.levelDescription()
                      << "          " << std::flush;
        }

        // -- 6.9 定期持久化 --
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - lastSaveTime).count();
        if (elapsed >= SAVE_INTERVAL_SEC) {
            dailyTracker.save();
            lastSaveTime = now;
        }

        // -- 6.10 等待下一次刷新 --
        std::this_thread::sleep_for(
            std::chrono::milliseconds(appCfg.monitor.refreshIntervalMs));
    }

    // --------------------------------------------------------
    //  7. 清理退出
    // --------------------------------------------------------
    std::cout << "\n[main] 正在退出，保存数据...\n";

    dailyTracker.save();
    notifier.shutdown();
    overlayWindow.destroy();
    renderer.shutdown();

    std::cout << "[main] NetGuard 已退出\n";
    return 0;
}

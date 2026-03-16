#pragma once

// ============================================================
//  Notifier.h
//  职责：执行警报动作，是警报系统的"输出端"
//        接收 AlertManager 判断好的状态，决定如何通知用户
//
//  支持的通知方式：
//    1. Windows 系统托盘气泡通知（Shell_NotifyIcon）
//    2. 窗口颜色信号（通过回调通知 OverlayWindow 变色）
//    3. 控制台颜色输出（调试模式下）
//
//  防重复通知机制：
//    同一等级只在首次进入时通知一次，不每秒重复弹窗
//    超限状态下每隔 reminderIntervalSec 秒重复提醒一次
// ============================================================

#include "../alert/AlertManager.h"

#include <string>
#include <functional>
#include <chrono>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace NetGuard {

// ------------------------------------------------------------
//  窗口颜色信号（传递给 OverlayWindow 的回调参数）
// ------------------------------------------------------------
enum class WindowColorSignal {
    NORMAL,     // 正常：深色半透明
    NOTICE,     // 注意：橙色
    WARNING,    // 警告：深橙/红色
    EXCEEDED,   // 超限：红色
    SPEED       // 速度警报：黄色高亮对应数值
};

// ============================================================
//  Notifier 类
//  - init()     ：初始化托盘图标（需要窗口句柄）
//  - shutdown() ：清理托盘图标
//  - notify()   ：根据 AlertStatus 决定是否发出通知
//  - setColorCallback()：注册窗口变色回调函数
// ============================================================
class Notifier {
public:
    Notifier()  = default;
    ~Notifier() = default;

    // 初始化系统托盘图标
    // hwnd：置顶窗口的句柄，用于关联托盘通知
    bool init(HWND hwnd);

    // 清理托盘资源（程序退出时调用）
    void shutdown();

    // 核心方法：根据最新警报状态决定是否发出通知
    // 内部处理防重复逻辑，只在状态变化或超时重提醒时触发
    void notify(const AlertStatus& status);

    // 注册窗口变色回调
    // 每次警报等级变化时，Notifier 会调用此回调通知 OverlayWindow
    void setColorCallback(std::function<void(WindowColorSignal)> cb);

    // 设置超限状态下的重复提醒间隔（秒），默认 300 秒（5分钟）
    void setReminderInterval(int seconds);

private:
    // 发送 Windows 系统气泡通知
    void showBalloonTip(const std::string& title,
                        const std::string& message,
                        DWORD              iconType = NIIF_INFO);

    // 根据警报等级映射颜色信号
    static WindowColorSignal levelToColor(AlertLevel level);

    // 控制台调试输出（带颜色）
    static void consoleLog(const std::string& msg, AlertLevel level);

    HWND       m_hwnd       = nullptr;
    bool       m_trayInited = false;

    // 防重复：记录上一次已通知的等级
    AlertLevel m_lastNotifiedLevel = AlertLevel::NORMAL;
    bool       m_lastSpeedAlert    = false;

    // 超限重复提醒计时
    std::chrono::steady_clock::time_point m_lastReminderTime;
    int m_reminderIntervalSec = 300;

    // 窗口变色回调
    std::function<void(WindowColorSignal)> m_colorCallback;
};

} // namespace NetGuard

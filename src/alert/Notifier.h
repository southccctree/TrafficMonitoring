#pragma once

// ============================================================
//  Notifier.h  v2
//  v2 修复：
//    - 统一冷却计时器，所有警报（含速度警报）触发后
//      至少等待 cooldownSec 秒才能再次弹窗
//    - 超限状态下每隔 reminderIntervalSec 秒重复提醒
//    - 默认冷却时间 600 秒（10 分钟）
// ============================================================

#include "../alert/AlertManager.h"

#include <string>
#include <functional>
#include <chrono>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

namespace NetGuard {

enum class WindowColorSignal {
    NORMAL, NOTICE, WARNING, EXCEEDED, SPEED
};

class Notifier {
public:
    Notifier()  = default;
    ~Notifier() = default;

    bool init(HWND hwnd);
    void shutdown();

    // 根据警报状态决定是否发出通知
    void notify(const AlertStatus& status);

    void setColorCallback(std::function<void(WindowColorSignal)> cb);

    // 冷却时间（秒）：两次弹窗之间的最短间隔，默认 600s（10分钟）
    void setCooldown(int seconds)        { m_cooldownSec = seconds; }

    // 超限状态下的重复提醒间隔（秒），默认与冷却时间相同
    void setReminderInterval(int seconds){ m_reminderIntervalSec = seconds; }

private:
    void showBalloonTip(const std::string& title,
                        const std::string& message,
                        DWORD              iconType = NIIF_INFO);

    static WindowColorSignal levelToColor(AlertLevel level);
    static void consoleLog(const std::string& msg, AlertLevel level);

    HWND m_hwnd       = nullptr;
    bool m_trayInited = false;

    // 上一次通知的等级（用于检测变化）
    AlertLevel m_lastNotifiedLevel = AlertLevel::NORMAL;
    bool       m_lastSpeedAlert    = false;

    // 统一冷却计时器：上一次弹窗的时间点
    std::chrono::steady_clock::time_point m_lastNotifyTime;
    bool m_everNotified        = false; // 是否已经弹过至少一次

    int m_cooldownSec          = 600;   // 弹窗冷却时间（秒）
    int m_reminderIntervalSec  = 600;   // 超限重复提醒间隔（秒）

    std::function<void(WindowColorSignal)> m_colorCallback;
};

} // namespace NetGuard

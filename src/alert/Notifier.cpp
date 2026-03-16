// ============================================================
//  Notifier.cpp
//  职责：实现系统托盘通知、窗口变色回调、控制台日志
//  依赖：Shell32（Shell_NotifyIcon）、Windows SDK
// ============================================================

#include "Notifier.h"

#include <iostream>
#include <sstream>
#include <shellapi.h>

#pragma comment(lib, "Shell32.lib")

// 托盘图标 ID（程序内唯一）
static constexpr UINT TRAY_ICON_ID = 1001;

// 自定义消息（托盘图标事件，目前仅注册，后续可扩展右键菜单）
static constexpr UINT WM_TRAY_ICON = WM_USER + 1;

namespace NetGuard {

// ============================================================
//  init() — 初始化系统托盘图标
// ============================================================
bool Notifier::init(HWND hwnd) {
    m_hwnd = hwnd;

    NOTIFYICONDATAW nid = {};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = hwnd;
    nid.uID              = TRAY_ICON_ID;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY_ICON;

    // 使用系统默认应用图标（后续可替换为自定义 .ico）
    nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);

    // 托盘悬浮提示文字
    wcscpy_s(nid.szTip, L"NetGuard — 流量监控");

    if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
        std::cerr << "[Notifier] 托盘图标初始化失败\n";
        return false;
    }

    m_trayInited        = true;
    m_lastReminderTime  = std::chrono::steady_clock::now();
    std::cout << "[Notifier] 托盘图标初始化成功\n";
    return true;
}

// ============================================================
//  shutdown() — 清理托盘图标
// ============================================================
void Notifier::shutdown() {
    if (!m_trayInited) return;

    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = m_hwnd;
    nid.uID    = TRAY_ICON_ID;
    Shell_NotifyIconW(NIM_DELETE, &nid);

    m_trayInited = false;
}

// ============================================================
//  notify() — 核心：根据警报状态决定是否发出通知
// ============================================================
void Notifier::notify(const AlertStatus& status) {

    // ---------- 流量等级通知 ----------
    if (status.levelChanged) {
        std::string title, message;

        switch (status.level) {
            case AlertLevel::NOTICE:
                title   = "NetGuard — 流量提醒";
                message = "今日流量已使用 "
                    + std::to_string(static_cast<int>(status.usagePercent))
                    + "%，请注意控制用量。";
                showBalloonTip(title, message, NIIF_INFO);
                break;

            case AlertLevel::WARNING:
                title   = "NetGuard — 流量警告";
                message = "今日流量已使用 "
                    + std::to_string(static_cast<int>(status.usagePercent))
                    + "%，即将耗尽！";
                showBalloonTip(title, message, NIIF_WARNING);
                break;

            case AlertLevel::EXCEEDED:
                title   = "NetGuard — 流量超限！";
                message = "今日流量已超出限额（"
                    + std::to_string(static_cast<int>(status.dailyUsedMB))
                    + " MB / "
                    + std::to_string(static_cast<int>(status.dailyLimitMB))
                    + " MB）";
                showBalloonTip(title, message, NIIF_ERROR);
                break;

            case AlertLevel::NORMAL:
                // 从警报恢复正常时（通常是跨日归零后），无需通知
                break;
        }

        m_lastNotifiedLevel = status.level;
        consoleLog(status.levelDescription(), status.level);
    }

    // ---------- 超限状态：定时重复提醒 ----------
    if (status.level == AlertLevel::EXCEEDED) {
        auto now     = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(
            now - m_lastReminderTime).count();

        if (elapsed >= static_cast<double>(m_reminderIntervalSec)) {
            showBalloonTip(
                "NetGuard — 流量超限",
                "今日流量持续超限，请检查网络使用情况！",
                NIIF_ERROR);
            m_lastReminderTime = now;
        }
    }

    // ---------- 速度警报通知 ----------
    if (status.speedChanged) {
        if (status.speedFlags.uploadExceeded) {
            std::ostringstream oss;
            oss << "当前上传速度 "
                << static_cast<int>(status.currentUploadKBps)
                << " KB/s 已超过设定阈值。";
            showBalloonTip("NetGuard — 上传速度过高", oss.str(), NIIF_WARNING);
        }
        if (status.speedFlags.downloadExceeded) {
            std::ostringstream oss;
            oss << "当前下载速度 "
                << static_cast<int>(status.currentDownloadKBps)
                << " KB/s 已超过设定阈值。";
            showBalloonTip("NetGuard — 下载速度过高", oss.str(), NIIF_WARNING);
        }
        m_lastSpeedAlert =
            status.speedFlags.uploadExceeded ||
            status.speedFlags.downloadExceeded;
    }

    // ---------- 触发窗口变色回调 ----------
    if (m_colorCallback) {
        // 速度警报优先级低于流量等级，只在流量正常时显示速度颜色
        WindowColorSignal sig = levelToColor(status.level);
        if (status.level == AlertLevel::NORMAL &&
            (status.speedFlags.uploadExceeded ||
             status.speedFlags.downloadExceeded)) {
            sig = WindowColorSignal::SPEED;
        }
        m_colorCallback(sig);
    }
}

// ============================================================
//  setColorCallback() — 注册窗口变色回调
// ============================================================
void Notifier::setColorCallback(std::function<void(WindowColorSignal)> cb) {
    m_colorCallback = std::move(cb);
}

// ============================================================
//  setReminderInterval() — 设置超限重复提醒间隔
// ============================================================
void Notifier::setReminderInterval(int seconds) {
    m_reminderIntervalSec = seconds;
}

// ============================================================
//  showBalloonTip() — 发送系统气泡通知
// ============================================================
void Notifier::showBalloonTip(const std::string& title,
                               const std::string& message,
                               DWORD              iconType)
{
    if (!m_trayInited) return;

    // 将 UTF-8 字符串转换为宽字符
    auto toWide = [](const std::string& s) -> std::wstring {
        if (s.empty()) return {};
        int len = MultiByteToWideChar(CP_UTF8, 0,
            s.c_str(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring w(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0,
            s.c_str(), static_cast<int>(s.size()), w.data(), len);
        return w;
    };

    NOTIFYICONDATAW nid = {};
    nid.cbSize      = sizeof(nid);
    nid.hWnd        = m_hwnd;
    nid.uID         = TRAY_ICON_ID;
    nid.uFlags      = NIF_INFO;
    nid.dwInfoFlags = iconType;

    // 气泡显示时长（毫秒），Windows 会自动限制最大值
    nid.uTimeout = 5000;

    std::wstring wTitle   = toWide(title);
    std::wstring wMessage = toWide(message);

    wcsncpy_s(nid.szInfoTitle, wTitle.c_str(),   _TRUNCATE);
    wcsncpy_s(nid.szInfo,      wMessage.c_str(), _TRUNCATE);

    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

// ============================================================
//  levelToColor() — 警报等级映射到窗口颜色信号
// ============================================================
WindowColorSignal Notifier::levelToColor(AlertLevel level) {
    switch (level) {
        case AlertLevel::NOTICE:   return WindowColorSignal::NOTICE;
        case AlertLevel::WARNING:  return WindowColorSignal::WARNING;
        case AlertLevel::EXCEEDED: return WindowColorSignal::EXCEEDED;
        default:                   return WindowColorSignal::NORMAL;
    }
}

// ============================================================
//  consoleLog() — 控制台带颜色输出（调试用）
// ============================================================
void Notifier::consoleLog(const std::string& msg, AlertLevel level) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    WORD color;
    switch (level) {
        case AlertLevel::NOTICE:   color = FOREGROUND_RED | FOREGROUND_GREEN; break; // 黄色
        case AlertLevel::WARNING:  color = FOREGROUND_RED | FOREGROUND_INTENSITY; break; // 亮红
        case AlertLevel::EXCEEDED: color = FOREGROUND_RED | FOREGROUND_INTENSITY |
                                           BACKGROUND_RED; break; // 红底红字
        default:                   color = FOREGROUND_RED |
                                           FOREGROUND_GREEN |
                                           FOREGROUND_BLUE; break; // 白色
    }

    SetConsoleTextAttribute(hConsole, color);
    std::cout << "[Alert] " << msg << "\n";
    // 恢复默认白色
    SetConsoleTextAttribute(hConsole,
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

} // namespace NetGuard

// ============================================================
//  Notifier.cpp  v2
//  修复：统一冷却计时器，防止频繁弹窗
// ============================================================

#include "Notifier.h"
#include <iostream>
#include <sstream>
#include <shellapi.h>

#pragma comment(lib, "Shell32.lib")

static constexpr UINT TRAY_ICON_ID = 1001;
static constexpr UINT WM_TRAY_ICON = WM_USER + 1;

namespace NetGuard {

// ============================================================
//  init()
// ============================================================
bool Notifier::init(HWND hwnd) {
    m_hwnd = hwnd;

    NOTIFYICONDATAW nid = {};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = hwnd;
    nid.uID              = TRAY_ICON_ID;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY_ICON;
    nid.hIcon            = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    wcscpy_s(nid.szTip, L"NetGuard — 流量监控");

    if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
        std::cerr << "[Notifier] 托盘图标初始化失败\n";
        return false;
    }

    m_trayInited = true;
    std::cout << "[Notifier] 托盘图标初始化成功\n";
    return true;
}

// ============================================================
//  shutdown()
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
//  notify() — 核心：冷却期内不重复弹窗
// ============================================================
void Notifier::notify(const AlertStatus& status) {
    auto now = std::chrono::steady_clock::now();

    // ---- 计算距上次通知的秒数 ----
    double sinceLastNotify = m_everNotified
        ? std::chrono::duration<double>(now - m_lastNotifyTime).count()
        : static_cast<double>(m_cooldownSec); // 首次视为冷却完毕

    bool cooldownOk = (sinceLastNotify >= static_cast<double>(m_cooldownSec));
    bool isLevelEscalation =
        (static_cast<int>(status.level) > static_cast<int>(m_lastNotifiedLevel));

    // ---- 窗口变色（不受冷却限制，实时响应）----
    if (m_colorCallback) {
        WindowColorSignal sig = levelToColor(status.level);
        if (status.level == AlertLevel::NORMAL &&
            (status.speedFlags.uploadExceeded ||
             status.speedFlags.downloadExceeded)) {
            sig = WindowColorSignal::SPEED;
        }
        m_colorCallback(sig);
    }

    // ---- 流量等级通知（等级变化 + 冷却完毕 才弹窗）----
    if (status.levelChanged && (cooldownOk || isLevelEscalation)) {
        std::string title, message;
        DWORD icon = NIIF_INFO;

        switch (status.level) {
            case AlertLevel::NOTICE:
                title   = "NetGuard — 流量提醒";
                message = "今日流量已使用 "
                    + std::to_string(static_cast<int>(status.usagePercent))
                    + "%，请注意控制用量。";
                icon = NIIF_INFO;
                break;
            case AlertLevel::WARNING:
                title   = "NetGuard — 流量警告";
                message = "今日流量已使用 "
                    + std::to_string(static_cast<int>(status.usagePercent))
                    + "%，即将耗尽！";
                icon = NIIF_WARNING;
                break;
            case AlertLevel::EXCEEDED:
                title   = "NetGuard — 流量超限！";
                message = "今日流量已超出限额（"
                    + std::to_string(static_cast<int>(status.dailyUsedMB))
                    + " / "
                    + std::to_string(static_cast<int>(status.dailyLimitMB))
                    + " MB）";
                icon = NIIF_ERROR;
                break;
            default:
                break;
        }

        if (!title.empty()) {
            showBalloonTip(title, message, icon);
            m_lastNotifyTime = now;
            m_everNotified   = true;
            m_lastNotifiedLevel = status.level;
            consoleLog(status.levelDescription(), status.level);
        }
    }

    // ---- 超限状态：按间隔重复提醒 ----
    if (status.level == AlertLevel::EXCEEDED) {
        double sinceReminder = m_everNotified
            ? std::chrono::duration<double>(now - m_lastNotifyTime).count()
            : 0.0;
        if (sinceReminder >= static_cast<double>(m_reminderIntervalSec)) {
            showBalloonTip(
                "NetGuard — 流量持续超限",
                "今日流量仍超出限额，请检查网络使用！",
                NIIF_ERROR);
            m_lastNotifyTime = now;
            m_everNotified   = true;
        }
    }

    // ---- 速度警报（变化 + 冷却完毕）----
    if (status.speedChanged && cooldownOk) {
        if (status.speedFlags.uploadExceeded) {
            std::ostringstream oss;
            oss << "当前上传速度 "
                << static_cast<int>(status.currentUploadKBps)
                << " KB/s 已超过设定阈值。";
            showBalloonTip("NetGuard — 上传速度过高", oss.str(), NIIF_WARNING);
            m_lastNotifyTime = now;
            m_everNotified   = true;
        }
        if (status.speedFlags.downloadExceeded) {
            std::ostringstream oss;
            oss << "当前下载速度 "
                << static_cast<int>(status.currentDownloadKBps)
                << " KB/s 已超过设定阈值。";
            showBalloonTip("NetGuard — 下载速度过高", oss.str(), NIIF_WARNING);
            m_lastNotifyTime = now;
            m_everNotified   = true;
        }
    }
}

// ============================================================
//  setColorCallback()
// ============================================================
void Notifier::setColorCallback(std::function<void(WindowColorSignal)> cb) {
    m_colorCallback = std::move(cb);
}

// ============================================================
//  showBalloonTip()
// ============================================================
void Notifier::showBalloonTip(const std::string& title,
                               const std::string& message,
                               DWORD              iconType)
{
    if (!m_trayInited) return;

    auto toWide = [](const std::string& s) -> std::wstring {
        if (s.empty()) return {};
        int len = MultiByteToWideChar(CP_UTF8, 0,
            s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring w(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0,
            s.c_str(), (int)s.size(), w.data(), len);
        return w;
    };

    NOTIFYICONDATAW nid = {};
    nid.cbSize      = sizeof(nid);
    nid.hWnd        = m_hwnd;
    nid.uID         = TRAY_ICON_ID;
    nid.uFlags      = NIF_INFO;
    nid.dwInfoFlags = iconType;
    nid.uTimeout    = 5000;

    auto wTitle   = toWide(title);
    auto wMessage = toWide(message);
    wcsncpy_s(nid.szInfoTitle, wTitle.c_str(),   _TRUNCATE);
    wcsncpy_s(nid.szInfo,      wMessage.c_str(), _TRUNCATE);

    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

// ============================================================
//  levelToColor()
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
//  consoleLog()
// ============================================================
void Notifier::consoleLog(const std::string& msg, AlertLevel level) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    WORD color;
    switch (level) {
        case AlertLevel::NOTICE:   color = FOREGROUND_RED | FOREGROUND_GREEN; break;
        case AlertLevel::WARNING:  color = FOREGROUND_RED | FOREGROUND_INTENSITY; break;
        case AlertLevel::EXCEEDED: color = FOREGROUND_RED | FOREGROUND_INTENSITY | BACKGROUND_RED; break;
        default:                   color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; break;
    }
    SetConsoleTextAttribute(h, color);
    std::cout << "[Alert] " << msg << "\n";
    SetConsoleTextAttribute(h, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

} // namespace NetGuard

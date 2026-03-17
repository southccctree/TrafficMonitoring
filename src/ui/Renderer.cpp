// ============================================================
//  Renderer.cpp
//  职责：实现置顶窗口内所有文字内容的 GDI 绘制
//  依赖：Gdi32（Win32 SDK 自带）
// ============================================================

#include "Renderer.h"

#include <sstream>
#include <iomanip>
#include <string>

#pragma comment(lib, "Gdi32.lib")

namespace NetGuard {

// ============================================================
//  析构函数
// ============================================================
Renderer::~Renderer() {
    shutdown();
}

// ============================================================
//  init() — 创建字体资源
// ============================================================
bool Renderer::init() {
    // 普通字体：14pt Consolas（等宽字体，速度数值对齐好看）
    m_fontNormal = CreateFontW(
        16, 0, 0, 0,
        FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        FIXED_PITCH | FF_MODERN,
        L"Consolas"
    );

    // 加粗字体：用于速度数值
    m_fontBold = CreateFontW(
        16, 0, 0, 0,
        FW_BOLD,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        FIXED_PITCH | FF_MODERN,
        L"Consolas"
    );

    // 小号字体：用于辅助信息（会话时长、百分比等）
    m_fontSmall = CreateFontW(
        13, 0, 0, 0,
        FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        FIXED_PITCH | FF_MODERN,
        L"Consolas"
    );

    if (!m_fontNormal || !m_fontBold || !m_fontSmall) {
        return false;
    }
    return true;
}

// ============================================================
//  shutdown() — 释放 GDI 资源
// ============================================================
void Renderer::shutdown() {
    if (m_fontNormal) { DeleteObject(m_fontNormal); m_fontNormal = nullptr; }
    if (m_fontBold)   { DeleteObject(m_fontBold);   m_fontBold   = nullptr; }
    if (m_fontSmall)  { DeleteObject(m_fontSmall);  m_fontSmall  = nullptr; }
}

// ============================================================
//  update() — 更新数据快照
// ============================================================
void Renderer::update(const SpeedResult&   speed,
                      const DailyRecord&   daily,
                      const SessionRecord& session,
                      const AlertStatus&   alert)
{
    m_speed   = speed;
    m_daily   = daily;
    m_session = session;
    m_alert   = alert;
}

// ============================================================
//  draw() — 主绘制函数，由 OverlayWindow 在 WM_PAINT 时调用
// ============================================================
void Renderer::draw(HDC hdc, RECT clientRect) {
    // 设置透明背景文字绘制模式
    SetBkMode(hdc, TRANSPARENT);

    int x = MARGIN_LEFT;
    int y = MARGIN_TOP;

    // ---- 上传速度 ----
    {
        // 上传箭头符号 ▲ + 速度数值
        bool  speedAlert = m_alert.speedFlags.uploadExceeded;
        COLORREF col     = speedAlert ? RGB(255, 80, 80) : RGB(100, 220, 100);

        SelectObject(hdc, m_fontBold);
        drawText(hdc, "\xe2\x96\xb2 " + m_speed.uploadFormatted(), x, y, col);
        y += LINE_HEIGHT;
    }

    // ---- 下载速度 ----
    {
        bool     speedAlert = m_alert.speedFlags.downloadExceeded;
        COLORREF col        = speedAlert ? RGB(255, 80, 80) : RGB(100, 180, 255);

        SelectObject(hdc, m_fontBold);
        drawText(hdc, "\xe2\x96\xbc " + m_speed.downloadFormatted(), x, y, col);
        y += LINE_HEIGHT;
    }

    // ---- 分隔线 ----
    drawSeparator(hdc, x, y + 4, clientRect.right - clientRect.left - x * 2);
    y += 14;

    // ---- 今日用量 ----
    {
        SelectObject(hdc, m_fontNormal);
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1);
        oss << "今日 "
            << m_daily.totalMB()
            << " / "
            << static_cast<int>(m_alert.dailyLimitMB)
            << " MB";

        // 接近限额时文字变色
        COLORREF col = RGB(220, 220, 220);
        if (m_alert.level == AlertLevel::EXCEEDED)      col = RGB(255, 80,  80);
        else if (m_alert.level == AlertLevel::WARNING)  col = RGB(255, 140, 40);
        else if (m_alert.level == AlertLevel::NOTICE)   col = RGB(255, 200, 60);

        drawText(hdc, oss.str(), x, y, col);
        y += LINE_HEIGHT;
    }

    // ---- VPN 用量 ----
    {
        SelectObject(hdc, m_fontNormal);
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1);
        oss << "VPN  "
            << m_alert.vpnUsedMB
            << " / "
            << static_cast<int>(m_alert.vpnLimitMB)
            << " MB";

        // VPN 剩余不足 20% 时变色
        double vpnPercent = (m_alert.vpnLimitMB > 0)
            ? (m_alert.vpnUsedMB / m_alert.vpnLimitMB * 100.0)
            : 0.0;
        COLORREF col = RGB(220, 220, 220);
        if      (vpnPercent >= 100.0) col = RGB(255, 80,  80);
        else if (vpnPercent >= 95.0)  col = RGB(255, 140, 40);
        else if (vpnPercent >= 80.0)  col = RGB(255, 200, 60);

        drawText(hdc, oss.str(), x, y, col);
        y += LINE_HEIGHT;
    }

    // ---- 会话用量 + 时长 ----
    {
        SelectObject(hdc, m_fontSmall);
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1);
        oss << "会话 "
            << m_session.totalMB()
            << " MB  "
            << m_session.elapsedFormatted();
        drawText(hdc, oss.str(), x, y, RGB(160, 160, 160));
        y += LINE_HEIGHT;
    }

    // ---- 分隔线 ----
    drawSeparator(hdc, x, y + 4, clientRect.right - clientRect.left - x * 2);
    y += 14;

    // ---- 警报状态 ----
    {
        SelectObject(hdc, m_fontSmall);

        // 用量百分比
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1);
        oss << m_alert.usagePercent << "%  "
            << m_alert.levelDescription();

        COLORREF col = levelColor(m_alert.level);
        drawText(hdc, oss.str(), x, y, col);
    }
}

// ============================================================
//  drawText() — 绘制单行 UTF-8 文字
// ============================================================
void Renderer::drawText(HDC              hdc,
                        const std::string& text,
                        int              x,
                        int              y,
                        COLORREF         color)
{
    // 将 UTF-8 转为宽字符（Windows GDI 使用 Unicode）
    int wLen = MultiByteToWideChar(CP_UTF8, 0,
        text.c_str(), static_cast<int>(text.size()),
        nullptr, 0);

    if (wLen <= 0) return;

    std::wstring wText(wLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0,
        text.c_str(), static_cast<int>(text.size()),
        wText.data(), wLen);

    SetTextColor(hdc, color);
    TextOutW(hdc, x, y, wText.c_str(), wLen);
}

// ============================================================
//  drawSeparator() — 绘制水平分隔线
// ============================================================
void Renderer::drawSeparator(HDC hdc, int x, int y, int width) {
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(80, 80, 80));
    HPEN old = static_cast<HPEN>(SelectObject(hdc, pen));

    MoveToEx(hdc, x,         y, nullptr);
    LineTo  (hdc, x + width, y);

    SelectObject(hdc, old);
    DeleteObject(pen);
}

// ============================================================
//  levelColor() — 警报等级对应的文字颜色
// ============================================================
COLORREF Renderer::levelColor(AlertLevel level) {
    switch (level) {
        case AlertLevel::NOTICE:   return RGB(255, 200, 60);
        case AlertLevel::WARNING:  return RGB(255, 140, 40);
        case AlertLevel::EXCEEDED: return RGB(255, 80,  80);
        default:                   return RGB(100, 220, 100);
    }
}

} // namespace NetGuard

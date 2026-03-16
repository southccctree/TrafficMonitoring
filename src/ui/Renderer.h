#pragma once

// ============================================================
//  Renderer.h
//  职责：在 OverlayWindow 提供的 HDC 上绘制所有文字内容
//        包括实时速度、今日用量、会话用量、警报状态
//        不涉及窗口创建或消息循环，只负责"画什么"
//
//  绘制布局（从上到下）：
//    ┌─────────────────────────┐
//    │  ▲ 12.3 KB/s            │  上传速度
//    │  ▼ 456.7 KB/s           │  下载速度
//    │  ─────────────────────  │
//    │  今日: 123.4 MB / 1024  │  今日累计 / 限额
//    │  VPN:  123.4 MB / 5120  │  VPN 用量 / 限额
//    │  会话: 45.6 MB  1h23m   │  会话用量 + 时长
//    │  ─────────────────────  │
//    │  ● 正常  (80%)          │  警报状态
//    └─────────────────────────┘
// ============================================================

#include "../stats/DailyTracker.h"
#include "../stats/SessionTracker.h"
#include "../monitor/SpeedCalculator.h"
#include "../alert/AlertManager.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace NetGuard {

// ============================================================
//  Renderer 类
//  - init()    ：创建绘制所需的字体资源
//  - shutdown()：释放 GDI 资源
//  - draw()    ：绘制所有内容到指定 HDC（由 OverlayWindow 回调调用）
//  - update()  ：更新待绘制的数据快照（主循环每秒调用）
// ============================================================
class Renderer {
public:
    Renderer()  = default;
    ~Renderer();

    // 初始化字体等 GDI 资源
    bool init();

    // 释放所有 GDI 资源
    void shutdown();

    // 更新数据快照（线程安全地暂存最新数据供 draw 使用）
    void update(const SpeedResult&   speed,
                const DailyRecord&   daily,
                const SessionRecord& session,
                const AlertStatus&   alert);

    // 绘制回调：由 OverlayWindow 在 WM_PAINT 时调用
    void draw(HDC hdc, RECT clientRect);

private:
    // 绘制单行文字（带颜色控制）
    void drawText(HDC              hdc,
                  const std::string& text,
                  int              x,
                  int              y,
                  COLORREF         color = RGB(255, 255, 255));

    // 绘制分隔线
    void drawSeparator(HDC hdc, int x, int y, int width);

    // 根据警报等级返回状态圆点颜色
    static COLORREF levelColor(AlertLevel level);

    // GDI 字体
    HFONT m_fontNormal  = nullptr;  // 普通数值字体
    HFONT m_fontBold    = nullptr;  // 加粗标题字体
    HFONT m_fontSmall   = nullptr;  // 小号辅助字体

    // 数据快照（由 update() 写入，draw() 读取）
    SpeedResult   m_speed;
    DailyRecord   m_daily;
    SessionRecord m_session;
    AlertStatus   m_alert;

    // 行高和左边距常量
    static constexpr int LINE_HEIGHT  = 22;
    static constexpr int MARGIN_LEFT  = 10;
    static constexpr int MARGIN_TOP   = 8;
};

} // namespace NetGuard

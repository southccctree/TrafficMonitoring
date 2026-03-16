#pragma once

// ============================================================
//  OverlayWindow.h
//  职责：创建并管理置顶无边框半透明小窗口
//        负责窗口生命周期、消息循环、拖动移动
//        不负责窗口内容的绘制（由 Renderer 负责）
//
//  窗口特性：
//    - 无边框、无标题栏
//    - 始终置顶（HWND_TOPMOST）
//    - 半透明（SetLayeredWindowAttributes）
//    - 支持鼠标拖动移动位置
//    - 支持运行时更新背景颜色（警报变色）
// ============================================================

#include "../alert/Notifier.h"

#include <string>
#include <functional>
#include <thread>
#include <atomic>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace NetGuard {

// ============================================================
//  OverlayWindow 类
//  - create()      ：创建窗口并启动消息循环线程
//  - destroy()     ：销毁窗口，停止线程
//  - setColor()    ：根据 WindowColorSignal 更新窗口背景色
//  - requestRedraw()：通知窗口重绘（主循环每秒调用一次）
//  - hwnd()        ：返回窗口句柄（供 Notifier 初始化托盘用）
//  - isRunning()   ：窗口是否还在运行
// ============================================================
class OverlayWindow {
public:
    OverlayWindow()  = default;
    ~OverlayWindow();

    // 创建窗口，启动独立消息循环线程
    // x, y      : 初始位置
    // w, h      : 窗口尺寸
    // opacity   : 透明度 0~255
    bool create(int x, int y, int w, int h, BYTE opacity);

    // 销毁窗口，终止消息线程
    void destroy();

    // 设置窗口背景颜色信号（由 Notifier 回调触发）
    void setColor(WindowColorSignal signal);

    // 触发窗口重绘（每次主循环刷新数据后调用）
    void requestRedraw();

    // 注册内容绘制回调（由 Renderer 提供具体绘制逻辑）
    // 每次 WM_PAINT 时调用
    void setPaintCallback(std::function<void(HDC, RECT)> cb);

    // 获取窗口句柄
    HWND hwnd() const { return m_hwnd; }

    // 窗口是否正在运行
    bool isRunning() const { return m_running.load(); }

    // 获取当前背景颜色（供 Renderer 使用）
    COLORREF backgroundColor() const { return m_bgColor; }

private:
    // 注册窗口类（只需执行一次）
    bool registerWindowClass(HINSTANCE hInstance);

    // 窗口消息循环（运行在独立线程中）
    void messageLoop(int x, int y, int w, int h, BYTE opacity);

    // Win32 窗口过程（静态函数，转发给实例方法）
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
                                    WPARAM wParam, LPARAM lParam);

    // 实例级消息处理
    LRESULT handleMessage(HWND hwnd, UINT msg,
                          WPARAM wParam, LPARAM lParam);

    // 根据颜色信号更新 m_bgColor
    void applyColorSignal(WindowColorSignal signal);

    HWND  m_hwnd    = nullptr;
    BYTE  m_opacity = 200;

    // 当前背景颜色
    COLORREF m_bgColor = RGB(20, 20, 20);   // 默认深色

    // 预定义颜色表
    static constexpr COLORREF COLOR_NORMAL   = RGB(20,  20,  20);   // 深灰
    static constexpr COLORREF COLOR_NOTICE   = RGB(180, 100,  0);   // 橙色
    static constexpr COLORREF COLOR_WARNING  = RGB(180,  30,  0);   // 深红
    static constexpr COLORREF COLOR_EXCEEDED = RGB(200,   0,  0);   // 纯红
    static constexpr COLORREF COLOR_SPEED    = RGB(140, 120,  0);   // 暗黄

    // 鼠标拖动状态
    bool  m_dragging   = false;
    POINT m_dragOffset = {0, 0};

    // 消息循环线程
    std::thread       m_thread;
    std::atomic<bool> m_running{ false };

    // 内容绘制回调（由 Renderer 注册）
    std::function<void(HDC, RECT)> m_paintCallback;

    // 窗口类名
    static constexpr wchar_t CLASS_NAME[] = L"NetGuardOverlay";
};

} // namespace NetGuard

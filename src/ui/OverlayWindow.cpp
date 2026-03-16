// ============================================================
//  OverlayWindow.cpp
//  职责：实现置顶无边框半透明窗口的创建、消息处理、拖动逻辑
//  依赖：Win32 API（User32、Gdi32）
// ============================================================

#include "OverlayWindow.h"

#include <iostream>
#include <stdexcept>

#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Gdi32.lib")

namespace NetGuard {

// ============================================================
//  析构函数 — 确保窗口和线程被正确清理
// ============================================================
OverlayWindow::~OverlayWindow() {
    destroy();
}

// ============================================================
//  create() — 启动消息循环线程，在线程内创建窗口
// ============================================================
bool OverlayWindow::create(int x, int y, int w, int h, BYTE opacity) {
    if (m_running.load()) return false;

    m_opacity  = opacity;
    m_running  = true;

    // 在独立线程中运行消息循环
    // 原因：Win32 窗口必须在创建它的线程中处理消息
    m_thread = std::thread([this, x, y, w, h, opacity]() {
        messageLoop(x, y, w, h, opacity);
    });

    // 等待窗口句柄就绪（最多等 2 秒）
    int waited = 0;
    while (m_hwnd == nullptr && waited < 2000) {
        Sleep(10);
        waited += 10;
    }

    if (m_hwnd == nullptr) {
        std::cerr << "[OverlayWindow] 窗口创建超时\n";
        m_running = false;
        return false;
    }

    return true;
}

// ============================================================
//  destroy() — 发送退出消息，等待线程结束
// ============================================================
void OverlayWindow::destroy() {
    if (!m_running.load()) return;

    if (m_hwnd) {
        PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }

    m_hwnd    = nullptr;
    m_running = false;
}

// ============================================================
//  setColor() — 更新背景颜色并触发重绘
// ============================================================
void OverlayWindow::setColor(WindowColorSignal signal) {
    applyColorSignal(signal);
    requestRedraw();
}

// ============================================================
//  requestRedraw() — 向窗口发送重绘请求
// ============================================================
void OverlayWindow::requestRedraw() {
    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }
}

// ============================================================
//  setPaintCallback() — 注册绘制回调
// ============================================================
void OverlayWindow::setPaintCallback(std::function<void(HDC, RECT)> cb) {
    m_paintCallback = std::move(cb);
}

// ============================================================
//  applyColorSignal() — 颜色信号映射到 COLORREF
// ============================================================
void OverlayWindow::applyColorSignal(WindowColorSignal signal) {
    switch (signal) {
        case WindowColorSignal::NOTICE:   m_bgColor = COLOR_NOTICE;   break;
        case WindowColorSignal::WARNING:  m_bgColor = COLOR_WARNING;  break;
        case WindowColorSignal::EXCEEDED: m_bgColor = COLOR_EXCEEDED; break;
        case WindowColorSignal::SPEED:    m_bgColor = COLOR_SPEED;    break;
        default:                          m_bgColor = COLOR_NORMAL;   break;
    }
}

// ============================================================
//  messageLoop() — 在独立线程中注册窗口类、创建窗口、跑消息循环
// ============================================================
void OverlayWindow::messageLoop(int x, int y, int w, int h, BYTE opacity) {
    HINSTANCE hInstance = GetModuleHandleW(nullptr);

    // 注册窗口类
    WNDCLASSEXW wc      = {};
    wc.cbSize           = sizeof(wc);
    wc.lpfnWndProc      = OverlayWindow::WndProc;
    wc.hInstance        = hInstance;
    wc.hCursor          = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground    = nullptr;          // 背景由 WM_PAINT 自绘
    wc.lpszClassName    = CLASS_NAME;

    // 允许重复注册（多次调用 create 时）
    RegisterClassExW(&wc);

    // 创建分层窗口（支持透明）
    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST         |   // 置顶
        WS_EX_LAYERED         |   // 分层（支持透明）
        WS_EX_TOOLWINDOW,         // 不在任务栏显示
        CLASS_NAME,
        L"NetGuard",
        WS_POPUP,                 // 无边框无标题栏
        x, y, w, h,
        nullptr, nullptr,
        hInstance,
        this                      // 传入 this 指针，供 WndProc 取回实例
    );

    if (!hwnd) {
        std::cerr << "[OverlayWindow] CreateWindowEx 失败，错误码: "
                  << GetLastError() << "\n";
        m_running = false;
        return;
    }

    // 设置透明度
    SetLayeredWindowAttributes(hwnd, 0, opacity, LWA_ALPHA);

    m_hwnd = hwnd;

    ShowWindow(hwnd, SW_SHOWNOACTIVATE);  // 显示但不抢焦点
    UpdateWindow(hwnd);

    // 标准 Win32 消息循环
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    m_running = false;
}

// ============================================================
//  WndProc() — 静态窗口过程，转发给实例方法
// ============================================================
LRESULT CALLBACK OverlayWindow::WndProc(HWND   hwnd,
                                         UINT   msg,
                                         WPARAM wParam,
                                         LPARAM lParam)
{
    OverlayWindow* self = nullptr;

    if (msg == WM_NCCREATE) {
        // 窗口创建时，从 CREATESTRUCT 取出 this 指针并存入 GWLP_USERDATA
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<OverlayWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<OverlayWindow*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->handleMessage(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================
//  handleMessage() — 实例级消息处理
// ============================================================
LRESULT OverlayWindow::handleMessage(HWND   hwnd,
                                      UINT   msg,
                                      WPARAM wParam,
                                      LPARAM lParam)
{
    switch (msg) {

    // ---- 绘制 ----
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        // 填充背景色
        HBRUSH bgBrush = CreateSolidBrush(m_bgColor);
        FillRect(hdc, &ps.rcPaint, bgBrush);
        DeleteObject(bgBrush);

        // 调用 Renderer 提供的绘制回调
        if (m_paintCallback) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            m_paintCallback(hdc, rc);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    // ---- 鼠标拖动：按下 ----
    case WM_LBUTTONDOWN: {
        m_dragging = true;
        POINT cursor;
        GetCursorPos(&cursor);
        RECT  winRect;
        GetWindowRect(hwnd, &winRect);
        m_dragOffset.x = cursor.x - winRect.left;
        m_dragOffset.y = cursor.y - winRect.top;
        SetCapture(hwnd);   // 捕获鼠标，防止移出窗口时丢失
        return 0;
    }

    // ---- 鼠标拖动：移动 ----
    case WM_MOUSEMOVE: {
        if (m_dragging) {
            POINT cursor;
            GetCursorPos(&cursor);
            SetWindowPos(hwnd, HWND_TOPMOST,
                cursor.x - m_dragOffset.x,
                cursor.y - m_dragOffset.y,
                0, 0,
                SWP_NOSIZE | SWP_NOACTIVATE);
        }
        return 0;
    }

    // ---- 鼠标拖动：松开 ----
    case WM_LBUTTONUP: {
        if (m_dragging) {
            m_dragging = false;
            ReleaseCapture();
        }
        return 0;
    }

    // ---- 关闭窗口 ----
    case WM_CLOSE: {
        DestroyWindow(hwnd);
        return 0;
    }

    // ---- 窗口销毁，退出消息循环 ----
    case WM_DESTROY: {
        PostQuitMessage(0);
        return 0;
    }

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // namespace NetGuard

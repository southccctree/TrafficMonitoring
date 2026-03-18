#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>

#include "../third_party/nlohmann/json.hpp"

using json = nlohmann::json;

#pragma comment(lib, "comdlg32.lib")

#define IDC_CLASH_CONFIG_PATH   101
#define IDC_CLASH_CONFIG_BROWSE 102
#define IDC_CLASH_API_PORT      103
#define IDC_CLASH_API_SECRET    104
#define IDC_DAILY_LIMIT         105
#define IDC_CLASH_LIMIT         106
#define IDC_ALERT_THRESHOLD     107
#define IDC_WARN_THRESHOLD      108
#define IDC_OPACITY             109
#define IDC_POS_X               110
#define IDC_POS_Y               111
#define IDC_NOTIFY_COOLDOWN     112
#define IDC_BTN_SAVE            201
#define IDC_BTN_CANCEL          202

static HWND g_hwnd = nullptr;
static std::string g_configPath = "config/netguard.json";
static json g_config;

static std::string GetEditText(HWND hEdit) {
    const int len = GetWindowTextLengthW(hEdit);
    if (len <= 0) {
        return "";
    }

    std::wstring ws(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(hEdit, ws.data(), len + 1);

    const int size = WideCharToMultiByte(
        CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return "";
    }

    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, ws.c_str(), -1, out.data(), size, nullptr, nullptr);

    if (!out.empty() && out.back() == '\0') {
        out.pop_back();
    }
    return out;
}

static void SetEditText(HWND hEdit, const std::string& text) {
    const int size = MultiByteToWideChar(
        CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (size <= 0) {
        SetWindowTextW(hEdit, L"");
        return;
    }

    std::wstring ws(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, ws.data(), size);
    SetWindowTextW(hEdit, ws.c_str());
}

static int GetEditInt(HWND hEdit, int minVal, int maxVal, int defaultVal) {
    const std::string s = GetEditText(hEdit);
    try {
        int v = std::stoi(s);
        if (v < minVal) return minVal;
        if (v > maxVal) return maxVal;
        return v;
    } catch (...) {
        return defaultVal;
    }
}

static bool LoadConfig() {
    std::ifstream f(g_configPath);
    if (!f.is_open()) {
        return false;
    }

    try {
        f >> g_config;
        return true;
    } catch (...) {
        return false;
    }
}

static void PopulateForm(HWND hwnd) {
    SetEditText(GetDlgItem(hwnd, IDC_CLASH_CONFIG_PATH),
                g_config.value("clash_config_path", ""));
    SetEditText(GetDlgItem(hwnd, IDC_CLASH_API_PORT),
                std::to_string(g_config.value("clash_api_port", 7890)));
    SetEditText(GetDlgItem(hwnd, IDC_CLASH_API_SECRET),
                g_config.value("clash_api_secret", ""));

    SetEditText(GetDlgItem(hwnd, IDC_DAILY_LIMIT),
                std::to_string(g_config.value("daily_limit_mb", 1024)));
    SetEditText(GetDlgItem(hwnd, IDC_CLASH_LIMIT),
                std::to_string(g_config.value("vpn_limit_mb", 5120)));
    SetEditText(GetDlgItem(hwnd, IDC_ALERT_THRESHOLD),
                std::to_string(g_config.value("alert_threshold_percent", 80)));
    SetEditText(GetDlgItem(hwnd, IDC_WARN_THRESHOLD),
                std::to_string(g_config.value("warn_threshold_percent", 95)));

    json window = g_config.value("window", json::object());
    SetEditText(GetDlgItem(hwnd, IDC_OPACITY),
                std::to_string(window.value("opacity", 200)));
    SetEditText(GetDlgItem(hwnd, IDC_POS_X),
                std::to_string(window.value("position_x", 20)));
    SetEditText(GetDlgItem(hwnd, IDC_POS_Y),
                std::to_string(window.value("position_y", 20)));
    SetEditText(GetDlgItem(hwnd, IDC_NOTIFY_COOLDOWN),
                std::to_string(g_config.value("notify_cooldown_sec", 600)));
}

static bool ValidateAndSave(HWND hwnd) {
    const int apiPort = GetEditInt(GetDlgItem(hwnd, IDC_CLASH_API_PORT), 1, 65535, 7890);
    const int daily = GetEditInt(GetDlgItem(hwnd, IDC_DAILY_LIMIT), 1, 999999, 1024);
    const int clash = GetEditInt(GetDlgItem(hwnd, IDC_CLASH_LIMIT), 1, 999999, 5120);
    const int alert = GetEditInt(GetDlgItem(hwnd, IDC_ALERT_THRESHOLD), 1, 99, 80);
    int warn = GetEditInt(GetDlgItem(hwnd, IDC_WARN_THRESHOLD), 1, 100, 95);
    const int opacity = GetEditInt(GetDlgItem(hwnd, IDC_OPACITY), 0, 255, 200);
    const int posX = GetEditInt(GetDlgItem(hwnd, IDC_POS_X), -9999, 9999, 20);
    const int posY = GetEditInt(GetDlgItem(hwnd, IDC_POS_Y), -9999, 9999, 20);
    const int cooldown = GetEditInt(GetDlgItem(hwnd, IDC_NOTIFY_COOLDOWN), 60, 86400, 600);

    if (warn < alert) {
        warn = (alert < 100) ? (alert + 1) : 100;
    }

    g_config["clash_config_path"] = GetEditText(GetDlgItem(hwnd, IDC_CLASH_CONFIG_PATH));
    g_config["clash_api_port"] = apiPort;
    g_config["clash_api_secret"] = GetEditText(GetDlgItem(hwnd, IDC_CLASH_API_SECRET));
    g_config["daily_limit_mb"] = daily;
    g_config["vpn_limit_mb"] = clash;
    g_config["alert_threshold_percent"] = alert;
    g_config["warn_threshold_percent"] = warn;
    g_config["notify_cooldown_sec"] = cooldown;
    g_config["window"]["opacity"] = opacity;
    g_config["window"]["position_x"] = posX;
    g_config["window"]["position_y"] = posY;

    try {
        std::ofstream f(g_configPath);
        if (!f.is_open()) {
            MessageBoxW(hwnd, L"无法写入配置文件，请检查文件权限。", L"保存失败", MB_ICONERROR);
            return false;
        }
        f << g_config.dump(4) << "\n";
        return true;
    } catch (...) {
        MessageBoxW(hwnd, L"JSON 序列化失败。", L"保存失败", MB_ICONERROR);
        return false;
    }
}

static HWND CreateLabelEdit(HWND parent,
                            HINSTANCE hi,
                            const wchar_t* label,
                            int ctrlId,
                            int x,
                            int y,
                            int editW = 80) {
    CreateWindowW(L"STATIC", label, WS_CHILD | WS_VISIBLE,
                  x, y + 2, 110, 18, parent, nullptr, hi, nullptr);
    return CreateWindowW(L"EDIT", nullptr,
                         WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                         x + 115, y, editW, 22, parent,
                         reinterpret_cast<HMENU>(static_cast<intptr_t>(ctrlId)),
                         hi, nullptr);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        const HINSTANCE hi = GetModuleHandleW(nullptr);
        int x = 16;
        int y = 14;

        CreateWindowW(L"STATIC", L"-- Clash 设置 -------------------",
                      WS_CHILD | WS_VISIBLE, x, y, 370, 16,
                      hwnd, nullptr, hi, nullptr);
        y += 22;

        CreateWindowW(L"STATIC", L"配置文件路径",
                      WS_CHILD | WS_VISIBLE, x, y + 2, 110, 18,
                      hwnd, nullptr, hi, nullptr);
        CreateWindowW(L"EDIT", nullptr,
                      WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                      x + 115, y, 200, 22, hwnd,
                      reinterpret_cast<HMENU>(IDC_CLASH_CONFIG_PATH), hi, nullptr);
        CreateWindowW(L"BUTTON", L"浏览",
                      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      x + 320, y, 50, 22, hwnd,
                      reinterpret_cast<HMENU>(IDC_CLASH_CONFIG_BROWSE), hi, nullptr);
        y += 28;

        CreateLabelEdit(hwnd, hi, L"API 端口", IDC_CLASH_API_PORT, x, y, 60);
        y += 28;
        CreateLabelEdit(hwnd, hi, L"API 密钥", IDC_CLASH_API_SECRET, x, y, 200);
        y += 36;

        CreateWindowW(L"STATIC", L"-- 流量限额 ---------------------",
                      WS_CHILD | WS_VISIBLE, x, y, 370, 16,
                      hwnd, nullptr, hi, nullptr);
        y += 22;

        CreateLabelEdit(hwnd, hi, L"每日限额 (MB)", IDC_DAILY_LIMIT, x, y, 80);
        y += 28;
        CreateLabelEdit(hwnd, hi, L"Clash 限额 (MB)", IDC_CLASH_LIMIT, x, y, 80);
        y += 28;

        CreateWindowW(L"STATIC", L"预警阈值 (%)",
                      WS_CHILD | WS_VISIBLE, x, y + 2, 110, 18,
                      hwnd, nullptr, hi, nullptr);
        CreateWindowW(L"EDIT", nullptr,
                      WS_CHILD | WS_VISIBLE | WS_BORDER,
                      x + 115, y, 50, 22, hwnd,
                      reinterpret_cast<HMENU>(IDC_ALERT_THRESHOLD), hi, nullptr);
        CreateWindowW(L"STATIC", L"警告阈值 (%)",
                      WS_CHILD | WS_VISIBLE, x + 180, y + 2, 100, 18,
                      hwnd, nullptr, hi, nullptr);
        CreateWindowW(L"EDIT", nullptr,
                      WS_CHILD | WS_VISIBLE | WS_BORDER,
                      x + 285, y, 50, 22, hwnd,
                      reinterpret_cast<HMENU>(IDC_WARN_THRESHOLD), hi, nullptr);
        y += 36;

        CreateWindowW(L"STATIC", L"-- 显示设置 ---------------------",
                      WS_CHILD | WS_VISIBLE, x, y, 370, 16,
                      hwnd, nullptr, hi, nullptr);
        y += 22;

        CreateLabelEdit(hwnd, hi, L"透明度 (0-255)", IDC_OPACITY, x, y, 50);
        y += 28;

        CreateWindowW(L"STATIC", L"窗口位置 X",
                      WS_CHILD | WS_VISIBLE, x, y + 2, 110, 18,
                      hwnd, nullptr, hi, nullptr);
        CreateWindowW(L"EDIT", nullptr,
                      WS_CHILD | WS_VISIBLE | WS_BORDER,
                      x + 115, y, 60, 22, hwnd,
                      reinterpret_cast<HMENU>(IDC_POS_X), hi, nullptr);
        CreateWindowW(L"STATIC", L"Y",
                      WS_CHILD | WS_VISIBLE, x + 185, y + 2, 20, 18,
                      hwnd, nullptr, hi, nullptr);
        CreateWindowW(L"EDIT", nullptr,
                      WS_CHILD | WS_VISIBLE | WS_BORDER,
                      x + 205, y, 60, 22, hwnd,
                      reinterpret_cast<HMENU>(IDC_POS_Y), hi, nullptr);
        y += 28;

        CreateLabelEdit(hwnd, hi, L"弹窗冷却 (秒)", IDC_NOTIFY_COOLDOWN, x, y, 70);
        y += 40;

        CreateWindowW(L"BUTTON", L"保存并关闭",
                      WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                      x + 160, y, 110, 28, hwnd,
                      reinterpret_cast<HMENU>(IDC_BTN_SAVE), hi, nullptr);
        CreateWindowW(L"BUTTON", L"取消",
                      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      x + 280, y, 80, 28, hwnd,
                      reinterpret_cast<HMENU>(IDC_BTN_CANCEL), hi, nullptr);

        PopulateForm(hwnd);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_CLASH_CONFIG_BROWSE: {
            wchar_t buf[MAX_PATH] = {};
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = L"YAML 文件 (*.yaml;*.yml)\0*.yaml;*.yml\0所有文件\0*.*\0";
            ofn.lpstrFile = buf;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                SetWindowTextW(GetDlgItem(hwnd, IDC_CLASH_CONFIG_PATH), buf);
            }
            return 0;
        }
        case IDC_BTN_SAVE:
            if (ValidateAndSave(hwnd)) {
                MessageBoxW(hwnd, L"配置已保存，重启 NetGuard 后生效。", L"保存成功", MB_ICONINFORMATION);
                DestroyWindow(hwnd);
            }
            return 0;
        case IDC_BTN_CANCEL:
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nCmdShow) {
    if (lpCmdLine && std::strlen(lpCmdLine) > 0) {
        g_configPath = lpCmdLine;
    }

    if (!LoadConfig()) {
        g_config = json::object();
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"NetGuardConfig";
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(
        0,
        L"NetGuardConfig",
        L"NetGuard 配置器",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        420,
        490,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}

# NetGuard — Copilot 修改指令 B1
## 目标：创建 NetGuardConfig.exe 独立配置程序

---

## 概述

新建一个独立的 Win32 GUI 程序 `NetGuardConfig`，用于可视化编辑
`netguard.json`，不依赖 NetGuard 主程序，可单独运行。

编译产物：`build/NetGuardConfig.exe`

---

## 任务一：新建源文件 `tools/NetGuardConfig.cpp`

在项目根目录新建 `tools/` 文件夹，创建 `NetGuardConfig.cpp`。

### 整体结构

```
WinMain
  ├── 读取 config/netguard.json（调用 LoadConfig）
  ├── 创建主窗口（CreateMainWindow）
  │     ├── Clash 设置区（API 端口、密钥、配置文件路径）
  │     ├── 限额设置区（每日限额、Clash 限额、预警阈值）
  │     ├── 显示设置区（透明度、窗口位置 X/Y、窗口高度）
  │     └── 按钮区（保存、取消）
  ├── 消息循环
  └── WM_COMMAND 处理（保存按钮 → ValidateAndSave）
```

### 界面布局（像素坐标，窗口 420×480）

```
┌────────────────────────────────────────┐
│  NetGuard 配置器                        │
│                                        │
│  ── Clash 设置 ──────────────────────  │
│  配置文件路径  [________________] [浏览] │
│  API 端口     [_____]                  │
│  API 密钥     [________________]       │
│                                        │
│  ── 流量限额 ────────────────────────  │
│  每日限额(MB)  [_____]                 │
│  Clash限额(MB) [_____]                 │
│  预警阈值(%)   [_____]  警告阈值(%)  [_]│
│                                        │
│  ── 显示设置 ────────────────────────  │
│  透明度(0-255) [_____]                 │
│  窗口位置 X    [_____]  Y  [_____]     │
│  弹窗冷却(秒)  [_____]                 │
│                                        │
│              [保存并关闭]  [取消]       │
└────────────────────────────────────────┘
```

### 控件 ID 定义

```cpp
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
```

### 完整代码实现

```cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>    // GetOpenFileName
#include <string>
#include <fstream>
#include <sstream>

// nlohmann/json（与主程序共用）
#include "../third_party/nlohmann/json.hpp"
using json = nlohmann::json;

#pragma comment(lib, "comdlg32.lib")

// ---- 全局变量 ----
static HWND g_hwnd = nullptr;
static std::string g_configPath = "config/netguard.json";
static json g_config;

// ---- 辅助函数 ----

// 读取编辑框文字（UTF-8）
std::string GetEditText(HWND hEdit) {
    int len = GetWindowTextLengthW(hEdit);
    if (len == 0) return "";
    std::wstring ws(len + 1, L'\0');
    GetWindowTextW(hEdit, ws.data(), len + 1);
    // 宽字符转 UTF-8
    int size = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1,
                                    nullptr, 0, nullptr, nullptr);
    std::string s(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1,
                        s.data(), size, nullptr, nullptr);
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

// 设置编辑框文字（UTF-8 → 宽字符）
void SetEditText(HWND hEdit, const std::string& text) {
    int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring ws(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, ws.data(), size);
    SetWindowTextW(hEdit, ws.c_str());
}

// 读取整数编辑框，带范围限制
int GetEditInt(HWND hEdit, int minVal, int maxVal, int defaultVal) {
    std::string s = GetEditText(hEdit);
    try {
        int v = std::stoi(s);
        if (v < minVal) return minVal;
        if (v > maxVal) return maxVal;
        return v;
    } catch (...) {
        return defaultVal;
    }
}

// 加载配置文件
bool LoadConfig() {
    std::ifstream f(g_configPath);
    if (!f.is_open()) return false;
    try {
        f >> g_config;
        return true;
    } catch (...) {
        return false;
    }
}

// 将表单数据回填到界面
void PopulateForm(HWND hwnd) {
    // Clash 设置
    SetEditText(GetDlgItem(hwnd, IDC_CLASH_CONFIG_PATH),
        g_config.value("clash_config_path", ""));
    SetEditText(GetDlgItem(hwnd, IDC_CLASH_API_PORT),
        std::to_string(g_config.value("clash_api_port", 9090)));
    SetEditText(GetDlgItem(hwnd, IDC_CLASH_API_SECRET),
        g_config.value("clash_api_secret", ""));

    // 限额设置
    SetEditText(GetDlgItem(hwnd, IDC_DAILY_LIMIT),
        std::to_string(g_config.value("daily_limit_mb", 1024)));
    SetEditText(GetDlgItem(hwnd, IDC_CLASH_LIMIT),
        std::to_string(g_config.value("clash_daily_limit_mb", 5120)));
    SetEditText(GetDlgItem(hwnd, IDC_ALERT_THRESHOLD),
        std::to_string(g_config.value("alert_threshold_percent", 80)));
    SetEditText(GetDlgItem(hwnd, IDC_WARN_THRESHOLD),
        std::to_string(g_config.value("warn_threshold_percent", 95)));

    // 显示设置
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

// 验证并保存
bool ValidateAndSave(HWND hwnd) {
    // 读取并校验各字段
    int apiPort  = GetEditInt(GetDlgItem(hwnd, IDC_CLASH_API_PORT),
                               1, 65535, 9090);
    int daily    = GetEditInt(GetDlgItem(hwnd, IDC_DAILY_LIMIT),
                               1, 999999, 1024);
    int clash    = GetEditInt(GetDlgItem(hwnd, IDC_CLASH_LIMIT),
                               1, 999999, 5120);
    int alert    = GetEditInt(GetDlgItem(hwnd, IDC_ALERT_THRESHOLD),
                               1, 99, 80);
    int warn     = GetEditInt(GetDlgItem(hwnd, IDC_WARN_THRESHOLD),
                               1, 100, 95);
    int opacity  = GetEditInt(GetDlgItem(hwnd, IDC_OPACITY),
                               0, 255, 200);
    int posX     = GetEditInt(GetDlgItem(hwnd, IDC_POS_X),
                               -9999, 9999, 20);
    int posY     = GetEditInt(GetDlgItem(hwnd, IDC_POS_Y),
                               -9999, 9999, 20);
    int cooldown = GetEditInt(GetDlgItem(hwnd, IDC_NOTIFY_COOLDOWN),
                               60, 86400, 600);

    // warn 必须 >= alert
    if (warn < alert) warn = alert + 1;

    // 更新 JSON
    g_config["clash_config_path"] = GetEditText(
        GetDlgItem(hwnd, IDC_CLASH_CONFIG_PATH));
    g_config["clash_api_port"]    = apiPort;
    g_config["clash_api_secret"]  = GetEditText(
        GetDlgItem(hwnd, IDC_CLASH_API_SECRET));
    g_config["daily_limit_mb"]            = daily;
    g_config["clash_daily_limit_mb"]      = clash;
    g_config["alert_threshold_percent"]   = alert;
    g_config["warn_threshold_percent"]    = warn;
    g_config["notify_cooldown_sec"]       = cooldown;
    g_config["window"]["opacity"]         = opacity;
    g_config["window"]["position_x"]      = posX;
    g_config["window"]["position_y"]      = posY;

    // 写回文件
    try {
        std::ofstream f(g_configPath);
        if (!f.is_open()) {
            MessageBoxW(hwnd, L"无法写入配置文件，请检查文件权限。",
                        L"保存失败", MB_ICONERROR);
            return false;
        }
        f << g_config.dump(4) << "\n";
        return true;
    } catch (...) {
        MessageBoxW(hwnd, L"JSON 序列化失败。",
                    L"保存失败", MB_ICONERROR);
        return false;
    }
}

// 创建 label + 编辑框 组合
HWND CreateLabelEdit(HWND parent, HINSTANCE hi,
                      const wchar_t* label, int ctrlId,
                      int x, int y, int editW = 80) {
    CreateWindowW(L"STATIC", label, WS_CHILD | WS_VISIBLE,
                  x, y + 2, 110, 18, parent, nullptr, hi, nullptr);
    return CreateWindowW(L"EDIT", nullptr,
                          WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                          x + 115, y, editW, 22, parent,
                          (HMENU)(intptr_t)ctrlId, hi, nullptr);
}

// 窗口过程
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
                          WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hi = GetModuleHandleW(nullptr);
        int x = 16, y = 14;

        // ---- Clash 设置区 ----
        CreateWindowW(L"STATIC", L"── Clash 设置 ──────────────────",
                      WS_CHILD | WS_VISIBLE, x, y, 370, 16,
                      hwnd, nullptr, hi, nullptr);
        y += 22;

        CreateWindowW(L"STATIC", L"配置文件路径",
                      WS_CHILD | WS_VISIBLE, x, y + 2, 110, 18,
                      hwnd, nullptr, hi, nullptr);
        CreateWindowW(L"EDIT", nullptr,
                      WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                      x + 115, y, 200, 22, hwnd,
                      (HMENU)IDC_CLASH_CONFIG_PATH, hi, nullptr);
        CreateWindowW(L"BUTTON", L"浏览",
                      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      x + 320, y, 50, 22, hwnd,
                      (HMENU)IDC_CLASH_CONFIG_BROWSE, hi, nullptr);
        y += 28;

        CreateLabelEdit(hwnd, hi, L"API 端口", IDC_CLASH_API_PORT,
                        x, y, 60);
        y += 28;
        CreateLabelEdit(hwnd, hi, L"API 密钥", IDC_CLASH_API_SECRET,
                        x, y, 200);
        y += 36;

        // ---- 限额设置区 ----
        CreateWindowW(L"STATIC", L"── 流量限额 ──────────────────────",
                      WS_CHILD | WS_VISIBLE, x, y, 370, 16,
                      hwnd, nullptr, hi, nullptr);
        y += 22;

        CreateLabelEdit(hwnd, hi, L"每日限额 (MB)", IDC_DAILY_LIMIT,
                        x, y, 80);
        y += 28;
        CreateLabelEdit(hwnd, hi, L"Clash 限额 (MB)", IDC_CLASH_LIMIT,
                        x, y, 80);
        y += 28;

        // 预警阈值和警告阈值同行
        CreateWindowW(L"STATIC", L"预警阈值 (%)",
                      WS_CHILD | WS_VISIBLE, x, y + 2, 110, 18,
                      hwnd, nullptr, hi, nullptr);
        CreateWindowW(L"EDIT", nullptr,
                      WS_CHILD | WS_VISIBLE | WS_BORDER,
                      x + 115, y, 50, 22, hwnd,
                      (HMENU)IDC_ALERT_THRESHOLD, hi, nullptr);
        CreateWindowW(L"STATIC", L"警告阈值 (%)",
                      WS_CHILD | WS_VISIBLE, x + 180, y + 2, 100, 18,
                      hwnd, nullptr, hi, nullptr);
        CreateWindowW(L"EDIT", nullptr,
                      WS_CHILD | WS_VISIBLE | WS_BORDER,
                      x + 285, y, 50, 22, hwnd,
                      (HMENU)IDC_WARN_THRESHOLD, hi, nullptr);
        y += 36;

        // ---- 显示设置区 ----
        CreateWindowW(L"STATIC", L"── 显示设置 ──────────────────────",
                      WS_CHILD | WS_VISIBLE, x, y, 370, 16,
                      hwnd, nullptr, hi, nullptr);
        y += 22;

        CreateLabelEdit(hwnd, hi, L"透明度 (0-255)", IDC_OPACITY,
                        x, y, 50);
        y += 28;

        // X / Y 同行
        CreateWindowW(L"STATIC", L"窗口位置 X",
                      WS_CHILD | WS_VISIBLE, x, y + 2, 110, 18,
                      hwnd, nullptr, hi, nullptr);
        CreateWindowW(L"EDIT", nullptr,
                      WS_CHILD | WS_VISIBLE | WS_BORDER,
                      x + 115, y, 60, 22, hwnd,
                      (HMENU)IDC_POS_X, hi, nullptr);
        CreateWindowW(L"STATIC", L"Y",
                      WS_CHILD | WS_VISIBLE, x + 185, y + 2, 20, 18,
                      hwnd, nullptr, hi, nullptr);
        CreateWindowW(L"EDIT", nullptr,
                      WS_CHILD | WS_VISIBLE | WS_BORDER,
                      x + 205, y, 60, 22, hwnd,
                      (HMENU)IDC_POS_Y, hi, nullptr);
        y += 28;

        CreateLabelEdit(hwnd, hi, L"弹窗冷却 (秒)", IDC_NOTIFY_COOLDOWN,
                        x, y, 70);
        y += 40;

        // ---- 按钮区 ----
        CreateWindowW(L"BUTTON", L"保存并关闭",
                      WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                      x + 160, y, 110, 28, hwnd,
                      (HMENU)IDC_BTN_SAVE, hi, nullptr);
        CreateWindowW(L"BUTTON", L"取消",
                      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                      x + 280, y, 80, 28, hwnd,
                      (HMENU)IDC_BTN_CANCEL, hi, nullptr);

        // 填充表单
        PopulateForm(hwnd);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_CLASH_CONFIG_BROWSE: {
            // 打开文件选择对话框
            wchar_t buf[MAX_PATH] = {};
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = hwnd;
            ofn.lpstrFilter = L"YAML 文件 (*.yaml;*.yml)\0*.yaml;*.yml\0所有文件\0*.*\0";
            ofn.lpstrFile   = buf;
            ofn.nMaxFile    = MAX_PATH;
            ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                SetWindowTextW(GetDlgItem(hwnd, IDC_CLASH_CONFIG_PATH), buf);
            }
            return 0;
        }
        case IDC_BTN_SAVE:
            if (ValidateAndSave(hwnd)) {
                MessageBoxW(hwnd, L"配置已保存，重启 NetGuard 后生效。",
                            L"保存成功", MB_ICONINFORMATION);
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

// WinMain
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE,
                   LPSTR lpCmdLine, int nCmdShow)
{
    // 支持命令行参数指定配置文件路径
    if (lpCmdLine && strlen(lpCmdLine) > 0) {
        g_configPath = lpCmdLine;
    }

    if (!LoadConfig()) {
        // 配置文件不存在时，使用空 JSON 对象（保存时会创建）
        g_config = json::object();
    }

    // 注册窗口类
    WNDCLASSEXW wc   = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"NetGuardConfig";
    RegisterClassExW(&wc);

    // 创建主窗口
    g_hwnd = CreateWindowExW(
        0, L"NetGuardConfig", L"NetGuard 配置器",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 490,
        nullptr, nullptr, hInstance, nullptr);

    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
```

---

## 任务二：新增编译任务到 tasks.json

在 `.vscode/tasks.json` 中新增一个独立的编译任务：

```json
{
    "type": "shell",
    "label": "NetGuardConfig: 构建",
    "command": "D:\\msys64\\ucrt64\\bin\\g++.exe",
    "args": [
        "-std=c++17",
        "-fdiagnostics-color=always",
        "-g",
        "-finput-charset=UTF-8",
        "-fexec-charset=UTF-8",
        "${workspaceFolder}\\tools\\NetGuardConfig.cpp",
        "-I", "${workspaceFolder}\\third_party",
        "-o", "${workspaceFolder}\\build\\NetGuardConfig.exe",
        "-static-libgcc",
        "-static-libstdc++",
        "-lcomctl32",
        "-lcomdlg32",
        "-luser32",
        "-lgdi32",
        "-mwindows"
    ],
    "options": {
        "cwd": "${workspaceFolder}"
    },
    "group": "build",
    "problemMatcher": ["$gcc"]
}
```

注意：`-mwindows` 使程序以 GUI 模式启动（不弹控制台窗口）。

---

## 任务三：更新 .gitignore

确认 `build/NetGuardConfig.exe` 已被 `build/` 规则覆盖（应已覆盖）。

---

## 使用说明（写入 README）

在 README 的"运行"章节新增：

```markdown
## 配置管理

运行 `build/NetGuardConfig.exe` 可打开图形化配置界面：

- **配置文件路径**：填入 Clash 的 config.yaml 路径，程序将自动读取 API 端口
- **API 端口**：若不使用配置文件自动读取，可手动填入 Clash 的 API 端口
- 修改后点击「保存并关闭」，重启 NetGuard 主程序即可生效
```

---

## 不需要修改的文件

- `src/` 目录下所有文件（配置程序完全独立）
- `config/netguard.json`（由配置程序运行时读写）

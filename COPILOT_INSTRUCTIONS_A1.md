# NetGuard — Copilot 修改指令 A1
## 目标：移除 TunnelProbe/Npcap，改用 Clash API 统计流量

---

## 背景

Clash 外部控制 API 的 `/traffic` 接口是 SSE 长连接推流：
```
GET http://127.0.0.1:{port}/traffic
Authorization: Bearer {secret}

← 每秒推送一条：
data: {"up":1234,"down":5678}
data: {"up":2345,"down":6789}
...（连接不断开）
```

`up`/`down` 是每秒的**瞬时字节速率**（bytes/sec），不是累计值。
在主循环每秒读一次，直接作为 delta 累加到 `clashDailyTracker` 即可。

---

## 任务一：新建 ClashApiProbe 模块

### 新建 `src/monitor/ClashApiProbe.h`

```cpp
#pragma once

// ============================================================
//  ClashApiProbe.h
//  职责：通过 Clash 外部控制 API 的 /traffic 接口
//        获取每秒实时流量数据
//  实现：WinHTTP 长连接，独立后台线程持续读取 SSE 推流
//  线程安全：通过原子变量向主线程暴露最新的每秒速率
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include <string>
#include <thread>
#include <atomic>
#include <cstdint>

#pragma comment(lib, "winhttp.lib")

namespace NetGuard {

class ClashApiProbe {
public:
    ClashApiProbe()  = default;
    ~ClashApiProbe() { stop(); }

    // 从配置文件解析 API 地址和密钥并启动连接
    // configPath: Clash 配置文件路径，如 "C:/Users/xxx/.config/clash/config.yaml"
    // 若解析失败或连接失败，静默返回 false，不崩溃
    bool start(const std::string& configPath);

    // 使用显式参数启动（不读配置文件）
    bool start(const std::string& host, uint16_t port,
               const std::string& secret);

    void stop();

    // 是否已连接到 Clash API
    bool isOnline() const { return m_online.load(); }

    // 最近一次推送的每秒上传字节数
    uint64_t lastUpBytes()   const { return m_lastUp.load(); }

    // 最近一次推送的每秒下载字节数
    uint64_t lastDownBytes() const { return m_lastDown.load(); }

    // 尝试重连（主循环每 30 秒调用一次）
    // 若已在线直接返回 true；若离线则重新解析配置并重连
    bool tryReconnect();

private:
    // 后台线程：维持 WinHTTP 长连接，持续解析 SSE 数据
    void sseLoop();

    // 解析 Clash config.yaml，提取 external-controller 和 secret
    // 使用简单字符串查找，不引入 YAML 解析库
    static bool parseClashConfig(const std::string& configPath,
                                  std::string&       outHost,
                                  uint16_t&          outPort,
                                  std::string&       outSecret);

    // 解析单行 SSE data，提取 up/down 数值
    // 输入: 'data: {"up":1234,"down":5678}'
    // 输出: up=1234, down=5678，返回 false 表示解析失败
    static bool parseSseLine(const std::string& line,
                              uint64_t& up, uint64_t& down);

    std::string  m_configPath;
    std::string  m_host   = "127.0.0.1";
    uint16_t     m_port   = 9090;
    std::string  m_secret;

    std::atomic<bool>     m_online  { false };
    std::atomic<bool>     m_stopFlag{ false };
    std::atomic<uint64_t> m_lastUp  { 0 };
    std::atomic<uint64_t> m_lastDown{ 0 };

    std::thread m_thread;
};

} // namespace NetGuard
```

### 新建 `src/monitor/ClashApiProbe.cpp`

#### parseClashConfig() 实现

使用逐行读取 + 字符串查找，不引入任何 YAML 库：

```cpp
bool ClashApiProbe::parseClashConfig(const std::string& configPath,
                                      std::string& outHost,
                                      uint16_t&    outPort,
                                      std::string& outSecret)
{
    std::ifstream file(configPath);
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        // 去除首尾空白
        auto trim = [](std::string s) {
            size_t start = s.find_first_not_of(" \t");
            size_t end   = s.find_last_not_of(" \t\r\n");
            return (start == std::string::npos)
                ? "" : s.substr(start, end - start + 1);
        };
        line = trim(line);

        // 解析 external-controller: 127.0.0.1:60082
        if (line.find("external-controller:") == 0) {
            std::string val = trim(line.substr(20));
            // 去除可能的引号
            if (!val.empty() && val.front() == '\'') val = val.substr(1);
            if (!val.empty() && val.back()  == '\'') val.pop_back();
            if (!val.empty() && val.front() == '"')  val = val.substr(1);
            if (!val.empty() && val.back()  == '"')  val.pop_back();

            auto colon = val.rfind(':');
            if (colon != std::string::npos) {
                outHost = val.substr(0, colon);
                outPort = static_cast<uint16_t>(
                    std::stoi(val.substr(colon + 1)));
            }
        }

        // 解析 secret: mypassword
        if (line.find("secret:") == 0) {
            std::string val = trim(line.substr(7));
            if (!val.empty() && val.front() == '\'') val = val.substr(1);
            if (!val.empty() && val.back()  == '\'') val.pop_back();
            if (!val.empty() && val.front() == '"')  val = val.substr(1);
            if (!val.empty() && val.back()  == '"')  val.pop_back();
            outSecret = val;
        }
    }

    return (outPort != 0);
}
```

#### parseSseLine() 实现

```cpp
bool ClashApiProbe::parseSseLine(const std::string& line,
                                  uint64_t& up, uint64_t& down)
{
    // 期望格式: data: {"up":1234,"down":5678}
    // 找到 "up": 和 "down":
    auto findVal = [&](const std::string& key) -> int64_t {
        auto pos = line.find(key);
        if (pos == std::string::npos) return -1;
        pos += key.size();
        // 跳过空白
        while (pos < line.size() && (line[pos] == ' ')) ++pos;
        // 读数字
        size_t end = pos;
        while (end < line.size() && std::isdigit(line[end])) ++end;
        if (end == pos) return -1;
        return std::stoll(line.substr(pos, end - pos));
    };

    int64_t u = findVal("\"up\":");
    int64_t d = findVal("\"down\":");
    if (u < 0 || d < 0) return false;

    up   = static_cast<uint64_t>(u);
    down = static_cast<uint64_t>(d);
    return true;
}
```

#### sseLoop() 实现

```cpp
void ClashApiProbe::sseLoop() {
    // 建立 WinHTTP 会话
    HINTERNET hSession = WinHttpOpen(
        L"NetGuard/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!hSession) { m_online = false; return; }

    // 转换 host 为宽字符
    std::wstring wHost(m_host.begin(), m_host.end());

    HINTERNET hConnect = WinHttpConnect(
        hSession, wHost.c_str(), m_port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        m_online = false;
        return;
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"GET", L"/traffic",
        nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, 0);  // 0 = HTTP（非 HTTPS）
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        m_online = false;
        return;
    }

    // 设置超时（连接5秒，读取5秒）
    WinHttpSetTimeouts(hRequest, 5000, 5000, 5000, 5000);

    // 添加 Authorization 头（若有 secret）
    if (!m_secret.empty()) {
        std::wstring auth = L"Authorization: Bearer " +
            std::wstring(m_secret.begin(), m_secret.end());
        WinHttpAddRequestHeaders(hRequest, auth.c_str(), (DWORD)-1,
                                  WINHTTP_ADDREQ_FLAG_ADD);
    }

    // 发送请求
    if (!WinHttpSendRequest(hRequest,
            WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, nullptr))
    {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        m_online = false;
        return;
    }

    m_online = true;
    std::string buffer;

    // 持续读取 SSE 数据流
    while (!m_stopFlag.load()) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &available)) break;
        if (available == 0) {
            Sleep(50);
            continue;
        }

        std::vector<char> chunk(available + 1, 0);
        DWORD read = 0;
        if (!WinHttpReadData(hRequest, chunk.data(), available, &read)) break;

        buffer.append(chunk.data(), read);

        // 按行处理
        size_t pos = 0;
        while (true) {
            size_t nl = buffer.find('\n', pos);
            if (nl == std::string::npos) break;

            std::string line = buffer.substr(pos, nl - pos);
            // 去除 \r
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            if (!line.empty()) {
                uint64_t up = 0, down = 0;
                if (parseSseLine(line, up, down)) {
                    m_lastUp.store(up,   std::memory_order_relaxed);
                    m_lastDown.store(down, std::memory_order_relaxed);
                }
            }

            pos = nl + 1;
        }
        // 保留未处理的部分
        buffer = buffer.substr(pos);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    m_online = false;
}
```

#### start() / stop() / tryReconnect() 实现

```cpp
bool ClashApiProbe::start(const std::string& configPath) {
    m_configPath = configPath;
    if (!parseClashConfig(configPath, m_host, m_port, m_secret)) {
        // 解析失败，保留默认值继续尝试
    }
    return start(m_host, m_port, m_secret);
}

bool ClashApiProbe::start(const std::string& host, uint16_t port,
                           const std::string& secret) {
    if (m_online.load()) return true;
    m_host   = host;
    m_port   = port;
    m_secret = secret;
    m_stopFlag = false;
    m_lastUp   = 0;
    m_lastDown = 0;
    m_thread = std::thread([this]() { sseLoop(); });
    // 等待最多 1 秒确认连接成功
    for (int i = 0; i < 10 && !m_online.load(); ++i) Sleep(100);
    return m_online.load();
}

void ClashApiProbe::stop() {
    if (!m_thread.joinable()) return;
    m_stopFlag = true;
    if (m_thread.joinable()) m_thread.join();
}

bool ClashApiProbe::tryReconnect() {
    if (m_online.load()) return true;
    if (m_thread.joinable()) m_thread.join();
    // 重新解析配置（Clash 可能换了端口）
    if (!m_configPath.empty()) {
        parseClashConfig(m_configPath, m_host, m_port, m_secret);
    }
    return start(m_host, m_port, m_secret);
}
```

---

## 任务二：修改 Config，新增 Clash 配置路径

### 修改 `src/config/Config.h`

在 `MonitorConfig` 中新增：
```cpp
std::string clashConfigPath = "";  // Clash config.yaml 路径，空字符串表示手动指定端口
std::string clashApiHost    = "127.0.0.1";
uint16_t    clashApiPort    = 9090;
std::string clashApiSecret  = "";
```

### 修改 `src/config/Config.cpp`

`load()` 新增读取：
```
"clash_config_path"  → monitor.clashConfigPath
"clash_api_host"     → monitor.clashApiHost
"clash_api_port"     → monitor.clashApiPort
"clash_api_secret"   → monitor.clashApiSecret
```

`save()` 同步写出。

### 修改 `config/netguard.json`

新增字段：
```json
"clash_config_path": "",
"clash_api_host": "127.0.0.1",
"clash_api_port": 9090,
"clash_api_secret": ""
```

说明：`clash_config_path` 非空时优先从配置文件自动解析端口；
为空时使用 `clash_api_host` + `clash_api_port` + `clash_api_secret`。

---

## 任务三：修改 main.cpp

### 删除所有 TunnelProbe 相关代码

删除：
- `#include "monitor/TunnelProbe.h"`
- `TunnelProbe clashProbe(...)` 实例
- `TunnelProbe orayProbe(...)` 实例
- 主循环中的 6.2 Clash Tunnel 采集段（旧版）

### 新增 ClashApiProbe 实例

```cpp
#include "monitor/ClashApiProbe.h"

NetGuard::ClashApiProbe clashApiProbe;
```

### 初始化阶段

```cpp
// 优先从 Clash 配置文件自动解析
if (!appCfg.monitor.clashConfigPath.empty()) {
    clashApiProbe.start(appCfg.monitor.clashConfigPath);
} else {
    clashApiProbe.start(appCfg.monitor.clashApiHost,
                        appCfg.monitor.clashApiPort,
                        appCfg.monitor.clashApiSecret);
}
bool wasClashOnline = clashApiProbe.isOnline();
```

### 新版主循环 6.2 段

```cpp
// -- 6.2 Clash API 采集 --
static int clashReconnectTick = 0;
if (++clashReconnectTick >= 30) {
    clashReconnectTick = 0;
    clashApiProbe.tryReconnect();
}

if (clashApiProbe.isOnline()) {
    // /traffic 接口推送的就是每秒字节数，直接作为 delta 使用
    uint64_t upBytes   = clashApiProbe.lastUpBytes();
    uint64_t downBytes = clashApiProbe.lastDownBytes();
    if (upBytes > 0 || downBytes > 0) {
        clashDailyTracker.addBytes(upBytes, downBytes);
    }
    clashDailyTracker.checkRollover();
}
```

注意：不需要差值计算，`lastUpBytes()` 本身就是每秒增量。

### Renderer 显示 Clash 离线状态

在传入 `renderer.update()` 之前：
```cpp
// 若 Clash 离线，向 alertStatus 写入标记
// 或直接在 Renderer 中根据 clashDaily.totalMB() == 0 且 !clashApiProbe.isOnline()
// 显示 "Clash Offline" 字样
```

---

## 任务四：修改 Renderer 显示 Clash 离线状态

### 修改 `src/ui/Renderer.h`

`update()` 新增一个参数：
```cpp
void update(..., bool clashOnline);
```

新增成员：
```cpp
bool m_clashOnline = false;
```

### 修改 `src/ui/Renderer.cpp`

Clash 行显示逻辑：
```cpp
if (!m_clashOnline) {
    // Clash 未连接时显示灰色 Offline
    SelectObject(hdc, m_fontNormal);
    drawText(hdc, "Cla  Offline", x, y, RGB(100, 100, 100));
    y += LINE_HEIGHT;
} else {
    // 正常显示用量
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1);
    oss << "Cla  " << m_clashDaily.totalMB()
        << " / " << static_cast<int>(m_alert.clashLimitMB) << " MB";
    drawText(hdc, oss.str(), x, y, levelColor(m_alert.clashLevel));
    y += LINE_HEIGHT;

    // 上传/下载小字
    if (m_clashDaily.totalMB() > 0.01) {
        SelectObject(hdc, m_fontSmall);
        std::ostringstream oss2;
        oss2 << std::fixed << std::setprecision(1);
        oss2 << "  ▲" << m_clashDaily.uploadMB()
             << " ▼" << m_clashDaily.downloadMB() << " MB";
        drawText(hdc, oss2.str(), x, y, RGB(140, 140, 140));
        y += LINE_HEIGHT - 4;
    }
}
```

---

## 任务五：清理构建文件

### 修改 `.vscode/tasks.json`

删除：
```
"${workspaceFolder}\\src\\monitor\\TunnelProbe.cpp",
```

新增：
```
"${workspaceFolder}\\src\\monitor\\ClashApiProbe.cpp",
```

删除链接库（若存在）：
```
"-lwpcap",
"-lPacket",
```

新增链接库：
```
"-lwinhttp",
```

删除 Include 路径（若存在）：
```
"-I", "${workspaceFolder}\\third_party\\npcap-sdk\\Include",
"-L", "${workspaceFolder}\\third_party\\npcap-sdk\\Lib\\x64",
```

### 修改 `CMakeLists.txt`

同步替换源文件和链接库。

---

## 任务六：删除 TunnelProbe 源文件

直接删除：
- `src/monitor/TunnelProbe.h`
- `src/monitor/TunnelProbe.cpp`

---

## 不需要修改的文件

- `NetworkMonitor.h/.cpp`（主网卡 Npcap 统计完全保留）
- `AlertManager.h/.cpp`
- `DailyTracker.h/.cpp`

---

## 验证

1. 在 `config/netguard.json` 填入 Clash 配置文件路径：
   ```json
   "clash_config_path": "C:\\Users\\xxx\\.config\\mihomo\\config.yaml"
   ```
   或直接填端口：
   ```json
   "clash_api_port": 60082
   ```

2. 启动 NetGuard，观察控制台是否出现连接成功日志

3. 打开 Clash TUN 播放 YouTube，Clash 行数值应与 Clash 客户端一致

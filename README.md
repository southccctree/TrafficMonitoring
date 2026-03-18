# NetGuard — 网络流量监控工具

一个运行在 Windows 桌面的轻量级流量监控程序，帮助你实时掌握**公网**流量消耗，自动过滤局域网流量，避免超出每日流量限额或 VPN 套餐额度。

---

## 功能一览

| 功能 | 说明 |
|------|------|
| 实时速度显示 | 每秒刷新当前上传 / 下载速度（KB/s 或 MB/s 自动切换） |
| 局域网流量过滤 | 基于 Npcap 逐包分析，自动跳过内网流量（串流、NAS、虚拟机等） |
| 累计流量统计 | 以自然日为周期统计今日公网上传 / 下载总量，次日自动归零 |
| 会话流量统计 | 记录本次程序运行期间的流量消耗，关闭后不保留 |
| 流量预警 | 今日累计流量接近或超过设定限额时触发警报 |
| 速度预警 | 上传或下载速度超过设定阈值时触发警报 |
| 置顶小窗口 | 半透明无边框小窗口常驻屏幕一角，支持鼠标拖动，不遮挡操作 |
| 系统托盘通知 | 触发警报时弹出 Windows 系统通知气泡 |
| 窗口变色提示 | 警报状态下窗口背景颜色改变：橙色→深红→红色 |
| 数据持久化 | 每日流量数据写入本地 JSON 文件，重启程序后继续累计 |
| 可配置参数 | 通过 JSON 配置文件自定义限额、阈值、刷新频率等 |

---

## 项目结构

```
NetGuard/
│
├── .gitignore
├── CMakeLists.txt
├── README.md
│
├── src/
│   ├── main.cpp                    # 程序入口，初始化各模块，驱动主循环
│   │
│   ├── monitor/
│   │   ├── NetworkMonitor.h/.cpp   # Npcap 抓包，过滤 LAN 流量，只统计公网字节
│   │   └── SpeedCalculator.h/.cpp  # 对快照做差值，计算实时上传/下载速度
│   │
│   ├── stats/
│   │   ├── DailyTracker.h/.cpp     # 以天为周期累计流量，负责持久化读写
│   │   └── SessionTracker.h/.cpp   # 统计本次运行会话的流量，不持久化
│   │
│   ├── alert/
│   │   ├── AlertManager.h/.cpp     # 比对数据与阈值，管理警报状态与规则
│   │   └── Notifier.h/.cpp         # 执行警报动作（系统通知、窗口变色）
│   │
│   ├── config/
│   │   └── Config.h/.cpp           # 解析配置文件，提供全局配置访问接口
│   │
│   └── ui/
│       ├── OverlayWindow.h/.cpp    # Win32 无边框置顶透明小窗口
│       └── Renderer.h/.cpp         # 在窗口内绘制速度、用量、状态信息
│
├── config/
│   ├── netguard.json               # 用户配置文件（不上传 Git）
│   └── netguard.json.example       # 配置模板（上传 Git，克隆后复制改名使用）
│
├── data/
│   └── daily_usage.json            # 每日流量持久化记录（不上传 Git）
│
├── build/                          # 编译输出目录（不上传 Git）
│   ├── NetGuard.exe
│   ├── libgcc_s_seh-1.dll          # MinGW 运行时（需手动复制，见下方说明）
│   ├── libstdc++-6.dll
│   └── libwinpthread-1.dll
│
└── third_party/                    # 第三方库（不上传 Git，需手动下载）
    ├── nlohmann/
    │   └── json.hpp                # nlohmann/json 单头文件库
    └── npcap-sdk/
        ├── Include/                # pcap.h 等头文件
        └── Lib/x64/                # wpcap.lib、Packet.lib
```

---

## 环境要求

| 项目 | 要求 |
|------|------|
| 操作系统 | Windows 10 / 11（64位） |
| 编译器 | MinGW-w64 GCC 11+（推荐通过 MSYS2 ucrt64 安装） |
| 构建工具 | VS Code + tasks.json 或 CMake 3.20+ |
| 运行时驱动 | **Npcap 1.87+**（必须安装，用于网络抓包） |

---

## 第一次编译前的准备

### 1. 安装 Npcap 驱动
前往 [https://npcap.com/#download](https://npcap.com/#download) 下载并安装：
- **Npcap 1.87 Installer** — 安装到本机，运行时必须
- **Npcap SDK 1.16** — 开发用，解压后放入项目目录

安装 Installer 时勾选 **"Install Npcap in WinPcap API-compatible Mode"**。

### 2. 放置 Npcap SDK
将下载的 SDK 解压，按以下结构放置：
```
third_party/
└── npcap-sdk/
    ├── Include/
    │   └── pcap.h  （及其他头文件）
    └── Lib/
        └── x64/
            ├── wpcap.lib
            └── Packet.lib
```

### 3. 下载 nlohmann/json
前往 [https://github.com/nlohmann/json/releases/latest](https://github.com/nlohmann/json/releases/latest)，
在 Assets 中下载 `json.hpp`（约 1MB 的单文件），放置到：
```
third_party/
└── nlohmann/
    └── json.hpp
```

### 4. 创建配置文件
复制配置模板并按需修改：
```bash
cp config/netguard.json.example config/netguard.json
```

---

## 编译

### 使用 VS Code（推荐）
按 `Ctrl+Shift+B` 直接构建，输出到 `build/NetGuard.exe`。

### 使用命令行
```bash
g++ -std=c++17 -g \
  src/main.cpp src/config/Config.cpp \
  src/monitor/NetworkMonitor.cpp src/monitor/SpeedCalculator.cpp \
  src/stats/DailyTracker.cpp src/stats/SessionTracker.cpp \
  src/alert/AlertManager.cpp src/alert/Notifier.cpp \
  src/ui/OverlayWindow.cpp src/ui/Renderer.cpp \
  -I src -I third_party -I third_party/npcap-sdk/Include \
  -L third_party/npcap-sdk/Lib/x64 \
  -o build/NetGuard.exe \
  -lwpcap -lPacket -liphlpapi -lws2_32 -lshell32 -luser32 -lgdi32
```

---

## 运行

### 首次运行
1. 将以下三个 MinGW 运行时 DLL 从 `D:\msys64\ucrt64\bin\` 复制到 `build\` 目录：
   ```
   libgcc_s_seh-1.dll
   libstdc++-6.dll
   libwinpthread-1.dll
   ```
2. 确保 `config/netguard.json` 和 `data/` 目录存在（程序会自动创建 data 目录）
3. **右键 `NetGuard.exe` → 以管理员身份运行**（Npcap 抓包需要管理员权限）

### 分发到其他电脑
将整个 `build\` 文件夹（含 exe 和三个 DLL）复制过去，目标电脑只需额外安装 **Npcap 驱动**即可运行，无需安装 MinGW 或任何其他运行时。

## 配置管理

运行 `build/NetGuardConfig.exe` 可打开图形化配置界面：

- **配置文件路径**：填入 Clash 的 config.yaml 路径，程序将自动读取 API 端口
- **API 端口**：若不使用配置文件自动读取，可手动填入 Clash 的 API 端口
- 修改后点击「保存并关闭」，重启 NetGuard 主程序即可生效

---

## 配置文件说明

配置文件位于 `config/netguard.json`，首次运行若文件不存在会自动生成默认值。

```json
{
    "daily_limit_mb": 1024,
    "vpn_limit_mb": 5120,
    "alert_threshold_percent": 80,
    "warn_threshold_percent": 95,
    "speed_alert_upload_kbps": 1024.0,
    "speed_alert_download_kbps": 2048.0,
    "refresh_interval_ms": 1000,
    "network_interface": "auto",
    "window": {
        "position_x": 20,
        "position_y": 20,
        "opacity": 200,
        "width": 260,
        "height": 160
    }
}
```

| 参数 | 说明 |
|------|------|
| `daily_limit_mb` | 每日公网流量限额（MB） |
| `vpn_limit_mb` | VPN 套餐剩余额度（MB），需手动更新 |
| `alert_threshold_percent` | 达到限额百分之几时开始预警，默认 80% |
| `warn_threshold_percent` | 达到限额百分之几时升级为警告，默认 95% |
| `speed_alert_upload_kbps` | 上传速度超过此值触发速度预警（KB/s） |
| `speed_alert_download_kbps` | 下载速度超过此值触发速度预警（KB/s） |
| `refresh_interval_ms` | 数据刷新间隔（毫秒），默认 1000ms |
| `network_interface` | 监控的网卡名，`"auto"` 自动选择物理网卡 |
| `window.position_x/y` | 置顶窗口初始位置（像素） |
| `window.opacity` | 窗口透明度，0（全透明）～255（不透明） |

**关于 `network_interface`**：若 `"auto"` 选择了错误的网卡，可在控制台日志中查看所有网卡列表，将正确的网卡名填入此字段，例如：
```json
"network_interface": "MediaTek Wi-Fi 6E MT7922 160MHz Wireless LAN Card"
```

---

## 置顶窗口说明

程序启动后窗口默认显示在屏幕左上角，显示内容如下：

```
▲ 12.3 KB/s           ← 上传速度（绿色，超速变红）
▼ 456.7 KB/s          ← 下载速度（蓝色，超速变红）
─────────────────────
今日 123.4 / 1024 MB  ← 今日公网用量 / 限额
VPN  123.4 / 5120 MB  ← VPN 用量 / 套餐限额
会话 45.6 MB  1h23m   ← 本次会话用量 + 运行时长
─────────────────────
80.0%  正常            ← 用量百分比 + 警报状态
```

- 支持鼠标拖动移动位置
- 不在任务栏显示

---

## 警报等级

| 等级 | 触发条件 | 窗口颜色 | 通知 |
|------|----------|----------|------|
| 正常 | 用量 < 80% | 深灰 | — |
| 注意 | 80% ～ 95% | 橙色 | 系统气泡通知（一次） |
| 警告 | 95% ～ 100% | 深红 | 系统气泡通知（一次） |
| 超限 | > 100% | 红色 | 系统通知 + 每 5 分钟重复提醒 |
| 速度警报 | 上传或下载超阈值 | 黄色（流量正常时） | 系统气泡通知（一次） |

---

## 局域网过滤说明

本程序使用 **Npcap** 进行逐包分析，以下流量**不会**计入统计：

- 内网串流（Moonlight、Parsec、Steam Link 等）
- 局域网文件传输（SMB、NAS 等）
- 虚拟机内网通信（VMware、Hyper-V 等）
- 蓝牙、回环等本地流量

过滤规则基于 IP 地址段：
```
10.0.0.0/8       （A 类私有）
172.16.0.0/12    （B 类私有）
192.168.0.0/16   （C 类私有）
127.0.0.0/8      （回环）
169.254.0.0/16   （链路本地）
224.0.0.0/4      （组播）
```

---

## 数据文件格式

`data/daily_usage.json` 示例：
```json
{
    "date": "2026-03-16",
    "upload_bytes": 52428800,
    "download_bytes": 314572800,
    "upload_mb": 50.0,
    "download_mb": 300.0,
    "total_mb": 350.0
}
```

每天第一次运行时程序自动检测日期变化，若已是新的一天则归零重新统计。

---

## 后续计划

- [ ] 历史流量折线图（按天统计）
- [ ] VPN 套餐余量自动同步
- [ ] 多网卡分别统计
- [ ] 开机自启动选项
- [ ] 导出流量报告（CSV）

---

## License

MIT License

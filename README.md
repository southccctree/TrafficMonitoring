# NetGuard — 网络流量监控工具

一个运行在 Windows 桌面的轻量级流量监控程序，帮助你实时掌握网络消耗，避免超出每日流量或 VPN 套餐额度。

---

## 功能一览

| 功能 | 说明 |
|------|------|
| 实时速度显示 | 每秒刷新当前上传 / 下载速度（KB/s 或 MB/s 自动切换） |
| 累计流量统计 | 以自然日为周期统计今日上传 / 下载总量，次日自动归零 |
| 会话流量统计 | 记录本次程序运行期间的流量消耗，关闭后不保留 |
| 流量预警 | 今日累计流量接近或超过设定限额时触发警报 |
| 速度预警 | 上传或下载速度超过设定阈值时触发警报 |
| 置顶小窗口 | 半透明无边框小窗口常驻屏幕一角，不遮挡正常操作 |
| 系统托盘通知 | 触发警报时弹出 Windows 系统通知气泡 |
| 窗口变色提示 | 警报状态下置顶窗口边框 / 背景颜色改变，直观醒目 |
| 数据持久化 | 每日流量数据写入本地 JSON 文件，重启程序后继续累计 |
| 可配置参数 | 通过 JSON 配置文件自定义限额、阈值、刷新频率等 |

---

## 项目结构

```
NetGuard/
│
├── CMakeLists.txt
├── README.md
│
├── src/
│   ├── main.cpp                    # 程序入口，初始化各模块，驱动主循环
│   │
│   ├── monitor/
│   │   ├── NetworkMonitor.h/.cpp   # 读取系统网卡原始字节数据
│   │   └── SpeedCalculator.h/.cpp  # 对原始数据做差值，计算实时速度
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
│   └── netguard.json               # 用户配置文件（限额、阈值、刷新频率）
│
└── data/
    └── daily_usage.json            # 每日流量持久化记录
```

---

## 配置文件说明

配置文件位于 `config/netguard.json`，首次运行自动生成默认值。

```json
{
  "daily_limit_mb": 1024,
  "vpn_limit_mb": 5120,
  "alert_threshold_percent": 80,
  "speed_alert_upload_kbps": 1024,
  "speed_alert_download_kbps": 2048,
  "refresh_interval_ms": 1000,
  "network_interface": "auto",
  "window": {
    "position_x": 20,
    "position_y": 20,
    "opacity": 200
  }
}
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `daily_limit_mb` | int | 每日流量限额（MB），超过此值触发累计预警 |
| `vpn_limit_mb` | int | VPN 套餐剩余额度（MB），需手动更新 |
| `alert_threshold_percent` | int | 达到限额的百分之几时开始预警，默认 80% |
| `speed_alert_upload_kbps` | int | 上传速度超过此值触发速度预警（KB/s） |
| `speed_alert_download_kbps` | int | 下载速度超过此值触发速度预警（KB/s） |
| `refresh_interval_ms` | int | 数据刷新间隔（毫秒），默认 1000ms |
| `network_interface` | string | 指定监控的网卡名，`"auto"` 自动选择流量最大的网卡 |
| `window.position_x/y` | int | 置顶窗口在屏幕上的初始位置（像素） |
| `window.opacity` | int | 窗口透明度，范围 0（全透明）~ 255（不透明） |

---

## 环境要求

| 项目 | 要求 |
|------|------|
| 操作系统 | Windows 10 / 11（64位） |
| 编译器 | MSVC 2019+ 或 MinGW-w64（GCC 11+） |
| 构建工具 | CMake 3.20 或以上 |
| 依赖库 | Windows SDK（自带）、iphlpapi、nlohmann/json（已内嵌） |

---

## 编译与运行

### 1. 克隆项目

```bash
git clone https://github.com/yourname/NetGuard.git
cd NetGuard
```

### 2. 配置构建

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
```

### 3. 编译

```bash
cmake --build build --config Release
```

### 4. 运行

```bash
./build/Release/NetGuard.exe
```

> **注意**：读取网卡数据可能需要管理员权限，建议右键以管理员身份运行。

---

## 数据文件说明

程序运行时会在 `data/daily_usage.json` 中记录每日流量：

```json
{
  "date": "2025-03-16",
  "upload_bytes": 52428800,
  "download_bytes": 314572800
}
```

每天第一次运行时，程序会检测日期是否变化，若变化则自动归零并开始新一天的统计。

---

## 置顶窗口说明

- 小窗口默认显示在屏幕左上角，可通过配置文件调整位置
- 支持鼠标拖动移动位置
- 正常状态：深色半透明背景，白色文字
- 预警状态：背景变为橙色，文字加粗
- 超限状态：背景变为红色，并触发系统通知

---

## 警报等级

| 等级 | 触发条件 | 表现 |
|------|----------|------|
| 正常 | 用量低于限额的 80% | 窗口正常显示 |
| 注意 | 用量达到限额的 80%～95% | 窗口变橙色 + 系统通知 |
| 警告 | 用量达到限额的 95%～100% | 窗口变红色 + 系统通知 |
| 超限 | 用量超过限额 | 窗口持续红色闪烁 + 重复通知 |
| 速度警报 | 上传或下载速度超过阈值 | 对应数值变红色显示 |

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

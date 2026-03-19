# NetGuard

Windows 桌面流量监控工具，面向两类流量：

1. 主网卡公网流量（自动过滤局域网）
2. Clash 实时流量（通过 Clash API /traffic）

## 功能

- 实时上传/下载速度显示（置顶透明小窗）
- 每日累计与会话累计统计
- 流量阈值预警、速度阈值预警、系统通知
- Clash 在线/离线状态显示与自动重连
- 图形化配置器 NetGuardConfig
- 配置与每日数据自动持久化

## 运行方式（必要步骤）

1. 安装 Npcap 驱动（WinPcap 兼容模式）。
2. 以管理员身份运行 build/NetGuard.exe。
3. 首次运行后会自动读取 exe 同目录下的 config/netguard.json。

说明：

- 程序已按 exe 目录定位配置，不依赖当前工作目录。
- 构建任务会自动同步 config/netguard.json 到 build/config/netguard.json。

## 配置方法

可选方式 A：图形化配置

- 运行 build/NetGuardConfig.exe。
- 修改后保存并重启主程序。

可选方式 B：手动编辑 JSON

- 文件位置：config/netguard.json（打包运行时同样可用 build/config/netguard.json）。
- Clash 配置优先级：

1. clash_config_path 非空时，优先从 Clash 配置文件解析 host/port/secret。
2. clash_config_path 为空时，使用 clash_api_host + clash_api_port + clash_api_secret。

推荐配置示例：

```json
{
    "daily_limit_mb": 1024,
    "vpn_limit_mb": 5120,
    "alert_threshold_percent": 80,
    "warn_threshold_percent": 95,
    "speed_alert_upload_kbps": 1024.0,
    "speed_alert_download_kbps": 2048.0,
    "notify_cooldown_sec": 600,
    "refresh_interval_ms": 1000,
    "network_interface": "auto",
    "vpn_interface": "",
    "clash_config_path": "",
    "clash_api_host": "127.0.0.1",
    "clash_api_port": 60082,
    "clash_api_secret": "",
    "window": {
        "position_x": 20,
        "position_y": 20,
        "opacity": 200,
        "width": 260,
        "height": 160
    }
}
```

## 使用方法

- 看实时速度：窗口上两行为上传/下载即时速率。
- 看今日总量：窗口与控制台会显示今日累计 MB。
- 看 Clash 状态：显示 online/offline，并展示 Clash 今日累计。
- 校准网卡：若 auto 选错网卡，启动日志里查看网卡列表后写入 network_interface。
- 调整告警：修改阈值与 notify_cooldown_sec，重启后生效。

## 打包与分发

- 分发时建议直接携带整个 build 目录。
- 目标机器只需安装 Npcap，即可直接运行，无需再次编译。
- 仓库不包含 third_party 外部库，不影响直接运行 exe。

## 可选：重新编译

- VS Code: 运行默认构建任务 NetGuard: 构建 (Debug)。
- 命令行或 CMake 方式可按项目内现有脚本执行。
- 若需重新编译，请先准备 third_party/npcap-sdk 与 third_party/nlohmann/json.hpp。

## License

MIT

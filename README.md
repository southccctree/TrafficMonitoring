# NetGuard

NetGuard is a lightweight Windows traffic monitor focused on two data sources:

1. Public internet traffic from your main network interface (LAN traffic filtered out)
2. Clash real-time traffic from Clash API /traffic

## What It Does

- Shows real-time upload and download speed in a small always-on-top overlay
- Tracks daily usage and current session usage
- Sends usage and speed alerts with system notifications
- Shows Clash online/offline status and reconnects automatically
- Provides a GUI configuration tool (NetGuardConfig)
- Persists config and usage data locally

## Executables (Not in Repository Root)

Both executables are generated in the build folder, not in the root directory:

- build/NetGuard.exe: Main monitoring application (traffic capture, overlay, alerts).
- build/NetGuardConfig.exe: GUI config editor (API port, thresholds, window options).

## Required Steps Before First Use

1. Install Npcap (WinPcap-compatible mode).
2. Find your Clash config.yaml file, then check external-controller.
3. Read the port number after 127.0.0.1: in external-controller.
4. Open build/NetGuardConfig.exe and fill that number into API Port.
5. Run build/NetGuard.exe as Administrator.

Example in config.yaml:

```yaml
external-controller: 127.0.0.1:60082
```

In this case, API Port should be 60082.

## Configuration Methods

Option A: GUI (recommended)

1. Run build/NetGuardConfig.exe.
2. Update settings and save.
3. Restart build/NetGuard.exe.

Option B: Edit JSON manually

- Config file path: config/netguard.json
- Packaged runtime path is also supported: build/config/netguard.json

Clash config priority:

1. If clash_config_path is not empty, NetGuard parses host/port/secret from Clash config file.
2. If clash_config_path is empty, NetGuard uses clash_api_host + clash_api_port + clash_api_secret.

Recommended config example:

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

## Daily Usage Tips

- Overlay top lines are real-time upload/download speed.
- Console and overlay both show daily total MB.
- Clash usage line shows online/offline and daily Clash traffic.
- If auto picks the wrong NIC, copy the correct interface name from startup logs into network_interface.

## Distribution

- Share the whole build directory for direct use.
- Target machine still needs Npcap installed.
- third_party is excluded from this repo and is only needed for rebuilding.

## Optional Rebuild

- VS Code: run default task NetGuard: Build (Debug).
- CLI/CMake: use existing project scripts.
- Rebuild requires third_party/npcap-sdk and third_party/nlohmann/json.hpp.

## License

MIT

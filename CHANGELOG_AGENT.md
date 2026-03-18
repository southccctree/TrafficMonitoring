# NetGuard 变更日志（Agent）

## 2026-03-18 23:13:35 +08:00

### 本次执行来源
- 按 `COPILOT_INSTRUCTIONS_A1.md` 执行改造任务：将 Clash 流量采集切换到 Clash API `/traffic`（SSE）。

### 代码修改
- 新增 Clash API 采集模块：
  - `src/monitor/ClashApiProbe.h`
  - `src/monitor/ClashApiProbe.cpp`
- 扩展配置结构与读写：
  - `src/config/Config.h`
  - `src/config/Config.cpp`
  - `config/netguard.json`
- 主流程改造（接入 ClashApiProbe、重连、累计、保存、退出清理）：
  - `src/main.cpp`
- 渲染改造（增加 Clash 在线状态与离线显示）：
  - `src/ui/Renderer.h`
  - `src/ui/Renderer.cpp`
- 构建脚本改造（加入 ClashApiProbe 与 winhttp）：
  - `.vscode/tasks.json`
  - `CMakeLists.txt`

### 构建验证
- 已执行任务：`NetGuard: 构建 (Debug)`
- 结果：编译通过，生成 `build/NetGuard.exe`。

### 兼容性说明（关键决策）
- A1 文档中提到删除 `-lwpcap`、`-lPacket` 与 Npcap include/lib 路径。
- 但当前 `src/monitor/NetworkMonitor.cpp` 仍直接调用 `pcap_*` 接口（主网卡统计仍依赖 Npcap）。
- 为避免链接失败，本次保留了 Npcap 相关链接与路径，仅新增 `-lwinhttp`。

### 未执行项说明
- `src/monitor/TunnelProbe.h/.cpp` 在当前仓库中不存在，因此无删除动作。

## 2026-03-18 23:38:02 +08:00

### 本次执行来源
- 按 `COPILOT_INSTRUCTIONS_B1.md` 执行：新增 NetGuardConfig 独立图形化配置程序。

### 代码修改
- 新增 GUI 配置器源码：
  - `tools/NetGuardConfig.cpp`
- 新增 VS Code 构建任务：
  - `.vscode/tasks.json`（`NetGuardConfig: 构建`）
- 更新文档：
  - `README.md`（新增“配置管理”章节）

### 构建验证
- 首次构建失败：UCRT 工具链下 `-static-libgcc -static-libstdc++` 导致链接 `wctob/btowc` 失败。
- 已修复：从任务参数中移除上述静态链接选项。
- 再次构建结果：通过，生成 `build/NetGuardConfig.exe`。

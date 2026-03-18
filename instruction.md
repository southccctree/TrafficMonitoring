1. 修改 src/config/Config.cpp — 统一绝对路径
不要使用相对路径，强制从 .exe 所在目录读取。

C++

// 在 Config::load() 中修改路径获取逻辑
#include <shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

std::string GetConfigPath() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath); // 移除文件名，保留目录
    
    std::wstring wPath = exePath;
    wPath += L"\\config\\netguard.json";
    
    // 转换为 UTF-8 string
    int size = WideCharToMultiByte(CP_UTF8, 0, wPath.c_str(), -1, NULL, 0, NULL, NULL);
    std::string path(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wPath.c_str(), -1, &path[0], size, NULL, NULL);
    if (!path.empty() && path.back() == '\0') path.pop_back();
    return path;
}
引用说明：此修改确保无论在 VSCode 还是 build 文件夹运行，读取的都是 .exe 旁的配置文件。

2. 修改 src/monitor/ClashApiProbe.cpp — 移除硬编码
确保 m_port 等成员变量不设默认值，必须由 start() 函数从配置中传入。

C++

// 找到 ClashApiProbe 构造函数或初始化位置
// 确保不再出现 m_port = 9090 或 7890 这种死代码
// 所有的端口必须由 main.cpp 调用 start 时从 appCfg.monitor.clashApiPort 传入
引用说明：解决你发现的“修改代码才生效”的根因。

3. 修改 src/main.cpp — 增加启动诊断日志
在连接前打印日志，方便用户 debug。

C++

// 在 clashApiProbe.start(...) 之前添加：
std::cout << "[Config] 尝试加载 Clash 配置: " 
          << (appCfg.monitor.clashConfigPath.empty() ? "手动模式" : appCfg.monitor.clashConfigPath) 
          << " 端口: " << appCfg.monitor.clashApiPort << std::endl;
4. 自动化构建任务 .vscode/tasks.json
增加文件拷贝逻辑，确保编译完后最新的 JSON 自动同步到 build/config。

JSON

// 在 args 数组末尾或作为独立 step 确保执行：
"command": "cmd",
"args": [
    "/C",
    "if not exist ${workspaceFolder}\\build\\config mkdir ${workspaceFolder}\\build\\config && xcopy /Y /I ${workspaceFolder}\\config\\netguard.json ${workspaceFolder}\\build\\config\\"
]
💡 为什么这样做就能解决“换电脑”的问题？
路径无关性：通过 GetModuleFileNameW，NetGuard 会像雷达一样自动定位自己身边的 config/netguard.json。哪怕你把整个 build 文件夹拷贝到 U 盘带走，它也能找到配置。

配置优先：我们彻底封死了代码里的“后门”（硬编码端口）。程序启动时，如果 JSON 里写的是 60082，它就绝不会去连 9090。

人性化反馈：新增的日志会直接告诉你：“嘿，我正在连 60082”。如果连接失败，你一眼就能看出是不是端口填错了。
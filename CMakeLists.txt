cmake_minimum_required(VERSION 3.20)
project(NetGuard VERSION 1.0.0 LANGUAGES CXX)

# ============================================================
#  编译器设置
# ============================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 生成 compile_commands.json（供 VS Code IntelliSense 使用，消除红线）
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================
#  源文件列表
# ============================================================
set(SOURCES
    src/main.cpp

    src/config/Config.cpp
    src/config/Config.h

    src/monitor/NetworkMonitor.cpp
    src/monitor/NetworkMonitor.h
    src/monitor/SpeedCalculator.cpp
    src/monitor/SpeedCalculator.h

    src/stats/DailyTracker.cpp
    src/stats/DailyTracker.h
    src/stats/SessionTracker.cpp
    src/stats/SessionTracker.h

    src/alert/AlertManager.cpp
    src/alert/AlertManager.h
    src/alert/Notifier.cpp
    src/alert/Notifier.h

    src/ui/OverlayWindow.cpp
    src/ui/OverlayWindow.h
    src/ui/Renderer.cpp
    src/ui/Renderer.h
)

# ============================================================
//  可执行文件
# ============================================================
add_executable(NetGuard ${SOURCES})

# ============================================================
#  Include 路径
# ============================================================
target_include_directories(NetGuard PRIVATE
    ${CMAKE_SOURCE_DIR}/src           # 模块间互相引用的根路径
    ${CMAKE_SOURCE_DIR}/third_party   # nlohmann/json.hpp 所在目录
)

# ============================================================
#  链接 Windows 系统库
# ============================================================
target_link_libraries(NetGuard PRIVATE
    iphlpapi    # GetIfTable2（网卡流量读取）
    ws2_32      # Winsock（部分网络 API 依赖）
    shell32     # Shell_NotifyIcon（托盘通知）
    user32      # Win32 窗口 API
    gdi32       # GDI 绘图
)

# ============================================================
#  编译选项
# ============================================================
if(MSVC)
    target_compile_options(NetGuard PRIVATE
        /W4         # 较高警告等级
        /utf-8      # 源文件编码 UTF-8（中文注释正常编译）
        /MP         # 多核并行编译
    )
else()
    # MinGW / GCC
    target_compile_options(NetGuard PRIVATE
        -Wall
        -Wextra
        -finput-charset=UTF-8
        -fexec-charset=UTF-8
    )
endif()

# ============================================================
#  Release 模式：使用 Windows 子系统（不弹控制台窗口）
#  Debug   模式：保留控制台窗口，方便查看日志
# ============================================================
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        set_target_properties(NetGuard PROPERTIES
            LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
    else()
        target_link_options(NetGuard PRIVATE -mwindows)
    endif()
endif()

# ============================================================
#  构建后自动复制配置目录到输出目录
#  确保运行时能找到 config/ 和 data/ 文件夹
# ============================================================
add_custom_command(TARGET NetGuard POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:NetGuard>/config"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:NetGuard>/data"
    COMMENT "创建运行时目录 config/ 和 data/"
)

# 若 config/netguard.json 已存在，复制到输出目录
if(EXISTS "${CMAKE_SOURCE_DIR}/config/netguard.json")
    add_custom_command(TARGET NetGuard POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/config/netguard.json"
            "$<TARGET_FILE_DIR:NetGuard>/config/netguard.json"
        COMMENT "复制 netguard.json 到输出目录"
    )
endif()

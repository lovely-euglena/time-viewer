// help.cpp
// 帮助信息显示模块实现

#include "help.h"
#include <shellapi.h>

// 将消息输出到控制台（支持分配/附加父控制台）
static void PrintToConsole(const wchar_t* msg) {
    HANDLE hCon = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hCon == INVALID_HANDLE_VALUE || hCon == nullptr) {
        // 尝试附加父进程控制台或分配新控制台
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
            AllocConsole();
        }
        hCon = GetStdHandle(STD_OUTPUT_HANDLE);
    }
    if (hCon && hCon != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteConsoleW(hCon, msg, (DWORD)wcslen(msg), &written, nullptr);
    }
    else {
        // 最后备选：使用 OutputDebugString
        OutputDebugStringW(msg);
    }
}

void ShowVersionAndExit() {
    const wchar_t* versionMsg =
        L"TimeDisplay 时间显示器 v2.0\n"
        L"Windows 11 风格网络时间同步工具\n"
        L"编译时间: " __DATE__ " " __TIME__ L"\n"
        L"开源地址: https://gitee.com/ygwill/time-viewer\n";
    PrintToConsole(versionMsg);
    exit(0);
}

void ShowHelpAndExit() {
    const wchar_t* helpMsg =
        L"\n\n\n===========================================================\n"
        L"TimeDisplay 时间显示器\n"
        L"Windows 11 风格网络时间同步工具\n"
        L"访问 https://gitee.com/ygwill/time-viwer/releases/ 获取更新\n"
        L"===========================================================\n"
        L"\n使用方法:\n"
        L"  TimeDisplay.exe [选项]\n"
        L"\n【命令行选项】\n"
        L"  -b, --no-button       隐藏所有按钮（全屏、退出、同步、置顶）\n"
        L"  -f, --full-screen     启动时自动进入全屏模式\n"
        L"  -k, --nok-key         禁用全局热键（F11, F5, F6, Ctrl+Shift+Q）\n"
        L"  -p, --topmost         启动时自动置顶窗口\n"
        L"  -t, --transparency    设置窗口透明度 (0-255, 0=完全透明, 255=不透明)\n"
        L"  -h, --help            显示此帮助信息\n"
        L"  -v, --version         显示版本信息\n"
        L"===========================================================\n"
        L"\n【热键说明】（默认启用，可用 -k 禁用）\n"
        L"  F11                   切换全屏/窗口模式\n"
        L"  F5                    手动同步网络时间（NTP）\n"
        L"  F6                    切换窗口置顶状态\n"
        L"  Ctrl+Shift+Q          退出程序\n"
        L"  双击标题栏（或触摸双击） 切换全屏模式\n"
        L"===========================================================\n"
        L"\n【功能特性】\n"
        L"  • NTP网络时间同步（自动/手动）\n"
        L"  • 多显示器DPI自适应\n"
        L"  • 窗口圆角效果（Windows 11风格）\n"
        L"  • 透明度调节\n"
        L"  • 触摸屏支持（双击切换全屏）\n"
        L"  • 自动同步间隔：1小时\n"
        L"===========================================================\n"
        L"\n【使用示例】\n"
        L"  TimeDisplay.exe                          # 普通模式启动\n"
        L"  TimeDisplay.exe -f -p                    # 全屏+置顶启动\n"
        L"  TimeDisplay.exe --no-button --transparency 200  # 无按钮半透明\n"
        L"  TimeDisplay.exe -k -b                    # 禁用热键并隐藏按钮\n"
        L"===========================================================\n"
        L"\n【项目信息】\n"
        L"  开源地址：https://gitee.com/ygwill/time-viewer\n"
        L"  问题反馈：请在Gitee上提交Issue\n"
        L"===========================================================\n"
        L"\n\n\n";
    PrintToConsole(helpMsg);
    exit(0);
}
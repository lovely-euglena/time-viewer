// main.cpp
// Windows 11风格时间显示主程序
// 修改：基于 DPI 动态调整所有尺寸上限（按钮、字体、窗口最小尺寸）

// 避免 windows.h 定义 min/max 宏，统一使用 std::min / std::max
#define NOMINMAX

// 必须最先包含 winsock2.h，避免与 windows.h 冲突
#include <winsock2.h>
#include <ws2tcpip.h>

// 然后包含其他 Windows 头文件
#include <windows.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>
#include <strsafe.h>
// 其余头文件
#include <string>
#include <algorithm>
#include <thread>
#include <atomic>
#include <new>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <conio.h>
#include "d2d_renderer.h"
#include "fullscreen.h"
#include "ntp_client.h"
#include "resource.h"
#include "debug_log.h"

// ===== Windows 7 兼容：手动定义 Windows 10/11 新增的类型和常量 =====
// 当 _WIN32_WINNT=0x0601 (Win7) 时，旧版 SDK 头文件不会导出这些类型/常量
// 我们通过 GetProcAddress 动态调用这些 API，只需编译期类型定义即可
// 注意：#ifndef 对 typedef/struct 名无效，只能检测宏定义
// Windows 11 SDK (10.0.22621+) 已自带这些定义，需避免重复

// --- DPI_AWARENESS_CONTEXT (Windows 8.1+) ---
// 不使用 DECLARE_HANDLE（它会定义 struct body + typedef），
// 改用 forward declaration + typedef，因为 C++17 允许重复的同名 typedef，
// 无论 SDK 是否已定义此类型都能编译通过。
struct DPI_AWARENESS_CONTEXT__;
typedef struct DPI_AWARENESS_CONTEXT__ *DPI_AWARENESS_CONTEXT;

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2  ((DPI_AWARENESS_CONTEXT)-4)
#endif

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE  ((DPI_AWARENESS_CONTEXT)-2)
#endif

// --- DWM 圆角常量 (Windows 11 build 22000+) ---
// 在 VS2022+ 的新版 SDK 中，DWMWA_WINDOW_CORNER_PREFERENCE 是
// DWMWINDOWATTRIBUTE 枚举常量而不是 #define 宏，DWM_WINDOW_CORNER_PREFERENCE
// 枚举也已在 dwmapi.h 中提供。此处只需确保 DWMWA_* 宏定义存在即可。
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

// --- 电源广播常量 (Windows XP+) ---
#ifndef PBT_APMRESUMEAUTOMATIC
#define PBT_APMRESUMEAUTOMATIC 0x0012
#endif

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")

// ===== Windows 7 兼容：STL 的 GetSystemTimePreciseAsFileTime 垫片 =====
// VS2026 (v145) 的 STL 中，std::thread/std::mutex 等会间接引用
// GetSystemTimePreciseAsFileTime（Windows 8+ 才提供的 API）。若走静态导入，Win7 上
// 进程启动时会报“无法定位程序输入点”。这里提供本地回退实现：
// 系统存在该 API 时动态调用，否则回退到 GetSystemTimeAsFileTime。
// x64：直接在下方为导入符号 __imp_... 提供定义；
// x86：导入符号带 "@4" 修饰，纯 C++ 无法定义，由 win7_stl_compat_x86.asm 引用本函数。
extern "C" void WINAPI TimeDisplay_FallbackGetSystemTimePreciseAsFileTime(LPFILETIME ft) {
    typedef void (WINAPI* PFN_GET_PRECISE_TIME)(LPFILETIME);
    static PFN_GET_PRECISE_TIME s_pfn = nullptr;
    static bool s_resolved = false;
    if (!s_resolved) {
        HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
        if (hKernel32) {
            s_pfn = reinterpret_cast<PFN_GET_PRECISE_TIME>(
                GetProcAddress(hKernel32, "GetSystemTimePreciseAsFileTime"));
        }
        s_resolved = true;
    }
    if (s_pfn) {
        s_pfn(ft);
    } else {
        ::GetSystemTimeAsFileTime(ft);
    }
}

#ifdef _M_X64
extern "C" void (WINAPI* const __imp_GetSystemTimePreciseAsFileTime)(LPFILETIME) =
    &TimeDisplay_FallbackGetSystemTimePreciseAsFileTime;
#endif // _M_X64

// 按钮ID定义
#define ID_FULLSCREEN_BTN  1001
#define ID_EXIT_BTN        1002
#define ID_SYNC_BTN        1003
#define ID_TOPMOST_BTN     1004

// 自定义消息：NTP同步完成
#define WM_NTP_SYNC_FINISHED (WM_USER + 100)

// 自动同步定时器ID
#define TIMER_AUTO_SYNC     3
#define AUTO_SYNC_INTERVAL_MS  3600000  // 1小时 = 3600000毫秒

// 按钮自动隐藏定时参数
#define AUTO_HIDE_IDLE_MS       5000  // 5秒无操作自动隐藏
#define AUTO_HIDE_MANUAL_MS    10000  // 手动激活后10秒自动隐藏

// 基准尺寸（96 DPI 下）
#define BASE_MIN_WINDOW_WIDTH     600
#define BASE_MIN_WINDOW_HEIGHT    400
#define BASE_MIN_BTN_WIDTH        70
#define BASE_MAX_BTN_WIDTH        150
#define BASE_MIN_BTN_HEIGHT       30
#define BASE_MAX_BTN_HEIGHT       60
#define BASE_MIN_TIME_FONT        40
#define BASE_MIN_DATE_FONT        15
#define BASE_MIN_WEEK_FONT        15
#define BASE_MIN_BTN_FONT         12

// 窗口过程函数声明
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

// 辅助函数：根据窗口 DPI 缩放基准值
inline int ScaleByDpi(int baseValue, HWND hWnd) {
    UINT dpi = GetWindowDpi(hWnd);
    float scale = dpi / 96.0f;
    return (int)(baseValue * scale);
}

// 全局变量
HFONT hLargeFont = nullptr;
HFONT hDateFont = nullptr;
HFONT hNormalFont = nullptr;    // 实际用作按钮字体
HFONT hSourceFont = nullptr;
HWND hMainWindow = nullptr;
HWND hFullscreenBtn = nullptr;
HWND hExitBtn = nullptr;
HWND hSyncBtn = nullptr;
HWND hTopmostBtn = nullptr;

// 嵌入字体相关
static HANDLE g_hEmbeddedFont = nullptr;
static bool g_useEmbeddedFont = false;
static const wchar_t* FONT_FAMILY_EMBEDDED = L"Source Han Sans Regular";
static const wchar_t* FONT_FAMILY_FALLBACK = L"Segoe UI";

std::atomic<bool> g_syncing{ false };
std::atomic<bool> g_exitFlag{ false };
std::thread g_syncThread;

// ===== 命令行参数标志 =====
bool g_hideButtons = false;
bool g_startFullscreen = false;
bool g_noHotKeys = false;
int  g_transparency = 255;
bool g_startTopmost = false;
bool g_topmost = false;

// ===== 按钮自动隐藏状态 =====
bool g_buttonsVisible = true;
ULONGLONG g_lastInteractionTime = 0;
bool g_manualShowActive = false;
ULONGLONG g_manualShowStartTime = 0;
bool g_givehelp = false;
bool g_showVersion = false;    // 新增：显示版本信息
bool g_forceGDI = false;       // --force-gdi / --disable-gpu：强制使用GDI而非GPU加速
bool g_noAutoHide = false;     // --no-auto-hide：禁用按钮自动隐藏
bool g_noAnim = false;         // --no-animation：禁用按钮显示/隐藏动画
bool g_disableTouch = false;   // --disable-touch：禁用双击/触摸双击切换全屏
bool g_showWarning = false;    // --show-warning：强制显示参数警告弹窗
bool g_ignoreWarning = false;  // --ignore-warning / -w：忽略快捷方式启动参数警告
// 启动环境：决定错误信息的报告方式（控制台 / 双击快捷方式 / 其它无法判定的环境）
enum class LaunchEnv { Console, Gui, Other };
LaunchEnv g_launchEnv = LaunchEnv::Other;
// ===== 命令行字体参数 =====
std::wstring g_cmdFontName;           // --font-name 参数指定的字体族名称
std::wstring g_cmdFontPath;           // --font-path 参数指定的字体文件路径
std::wstring g_fontFamilyToUse;       // 最终解析后实际使用的字体族名称
bool g_fontLoadedFromPath = false;    // 是否通过 --font-path 从外部文件加载了字体

// ===== 按钮动态尺寸相关全局变量 =====
int g_buttonWidth = 0;          // 当前按钮宽度（像素）
int g_buttonHeight = 0;         // 当前按钮高度（像素）

// ===== 网络时间存储 =====
static SYSTEMTIME g_networkTime = { 0 };
static bool g_hasNetworkTime = false;
static ULONGLONG g_syncBaseTick = 0;

struct FontSizes {
    int time;
    int date;
    int week;
    int button;
    int source;
};

// 函数声明
std::wstring GetTimeDateString();
void CreateButtons(HWND hWnd);
void UpdateButtonLayout(HWND hWnd);
void HandleTouchInput(HWND hWnd, WPARAM wParam, LPARAM lParam);
void CreateDynamicFonts(HWND hWnd);
void StartSync(HWND hWnd);
void SyncThreadProc(HWND hWnd);
FontSizes CalculateFontSizes(HWND hWnd);
void ToggleTopmost(HWND hWnd);
void ShowButtons(HWND hWnd);
void HideButtons(HWND hWnd);
void ResetInterActionTimer();
void CheckAutoHide(HWND hWnd);

int GetDisplayTime(SYSTEMTIME& st);
std::wstring GetTimeDateStringEx();
std::wstring GetTimeSourceString();

// ==================== 新增：帮助系统函数 ====================

// 将文本写入指定句柄。
// 控制台句柄用 WriteConsoleW 输出 Unicode；
// 管道/文件等重定向场景下 WriteConsoleW 会失败，此时按控制台/系统代码页
// 转换为多字节后用 WriteFile 输出，保证重定向到文件时内容不丢失。
static void WriteTextToHandle(HANDLE hOut, const wchar_t* msg) {
    if (!hOut || hOut == INVALID_HANDLE_VALUE || !msg) return;

    DWORD mode = 0;
    if (GetConsoleMode(hOut, &mode)) {
        DWORD written = 0;
        WriteConsoleW(hOut, msg, (DWORD)wcslen(msg), &written, nullptr);
        return;
    }

    UINT cp = GetConsoleOutputCP();
    if (cp == 0) cp = GetACP();
    int bytes = WideCharToMultiByte(cp, 0, msg, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) return; // 转换失败或空字符串
    std::string buf((size_t)(bytes - 1), '\0');
    WideCharToMultiByte(cp, 0, msg, -1, &buf[0], bytes, nullptr, nullptr);
    DWORD written = 0;
    WriteFile(hOut, buf.data(), (DWORD)buf.size(), &written, nullptr);
}

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
        WriteTextToHandle(hCon, msg);
    }
    else {
        // 最后备选：使用 OutputDebugString
        OutputDebugStringW(msg);
    }
}

// 将消息输出到标准错误（stderr），绝不新建控制台窗口：
// - stderr 已重定向到文件/管道：直接写入；
// - 控制台启动：临时附加父控制台后输出（附加完即分离）；
// - 双击/快捷方式等无控制台环境：仅 OutputDebugString。
static void WriteToStderr(const wchar_t* msg) {
    DebugLogRaw(msg);  // 记录到调试日志（未启用 --debug 时仅调试器可见）

    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hErr && hErr != INVALID_HANDLE_VALUE) {
        DWORD type = GetFileType(hErr);
        if (type == FILE_TYPE_DISK || type == FILE_TYPE_PIPE) {
            WriteTextToHandle(hErr, msg);
            return;
        }
        DWORD mode = 0;
        if (GetConsoleMode(hErr, &mode)) {
            WriteTextToHandle(hErr, msg);
            return;
        }
    }

    // 无可用 stderr：尝试附加父控制台（只附加、不新建）
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        HANDLE hConOut = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr);
        if (hConOut != INVALID_HANDLE_VALUE) {
            WriteTextToHandle(hConOut, msg);
            CloseHandle(hConOut);
        }
        FreeConsole();
    }
}

// 显示版本信息并退出
static void ShowVersionAndExit() {
    const wchar_t* versionMsg =
        L"Windows 11 风格网络时间同步工具\n"
        L"编译时间: " __DATE__ " " __TIME__ L"\n"
        L"开源地址: https://gitee.com/ygwill/time-viewer\n";
    PrintToConsole(versionMsg);
    exit(0);
}

// 分页输出帮助信息（每页 pageLines 行，支持鼠标滚轮和键盘）
static void ShowHelpAndExit() {
    const wchar_t* helpMsg =
        L"\n\n\n===========================================================\n"
        L"TimeDisplay 时间显示器\n"
        L"Windows 11 风格网络时间同步工具\n"
        L"访问 https://gitee.com/ygwill/time-viewer/releases/ 获取更新\n"
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
        L"  --show-warning        强制显示参数检查警告弹窗（命令行启动时也显示）\n"
        L"  -w, --ignore-warning  忽略快捷方式/双击启动时的参数警告弹窗\n"
        L"  --force-gdi           强制使用 GDI 软件渲染，不启用 Direct2D GPU 加速\n"
        L"  --disable-gpu         同上，强制禁用 GPU 加速（与 --force-gdi 等效）\n"
        L"  --disable-touch       禁用双击标题栏/触摸双击切换全屏功能\n"
        L"  --no-auto-hide        禁用按钮自动隐藏，按钮始终显示\n"
        L"  --no-animation       禁用按钮显示/隐藏的淡入淡出动画\n"
        L"  --font-name <名称>    指定显示字体（系统中已安装的字体族名称）\n"
        L"                        示例: --font-name \"Microsoft YaHei\"\n"
        L"  --font-path <路径>    从外部字体文件加载字体（支持 .ttf/.otf）\n"
        L"                        示例: --font-path \"C:\\Fonts\\CustomFont.ttf\"\n"
        L"                        可与 --font-name 组合使用以指定字体族名称\n"
        L"  --debug [路径]        输出调试日志（默认 exe 同目录 TimeDisplay.log）\n"
        L"                        路径可指定日志文件或目录；Release 下仅输出关键日志\n"
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
        L"  • 触摸屏支持（双击切换全屏，可用 --disable-touch 禁用）\n"
        L"  • 自动同步间隔：1小时\n"
        L"===========================================================\n"
        L"\n【使用示例】\n"
        L"  TimeDisplay.exe                          # 普通模式启动\n"
        L"  TimeDisplay.exe -f -p                    # 全屏+置顶启动\n"
        L"  TimeDisplay.exe --no-button --transparency 200  # 无按钮半透明\n"
        L"  TimeDisplay.exe -k -b                    # 禁用热键并隐藏按钮\n"
        L"  TimeDisplay.exe --font-name \"Microsoft YaHei\"  # 使用微软雅黑字体\n"
        L"  TimeDisplay.exe --font-path \"myfont.ttf\" --font-name \"MyFont\"  # 加载外部字体\n"
        L"  TimeDisplay.exe --debug                  # 输出调试日志\n"
        L"===========================================================\n"
        L"\n【项目信息】\n"
        L"  开源地址：https://gitee.com/ygwill/time-viewer\n"
        L"  问题反馈：请在Gitee上提交Issue\n"
        L"===========================================================\n"
        L"\n\n\n";

    // 获取控制台输出句柄
    HANDLE hCon = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hCon == INVALID_HANDLE_VALUE || hCon == nullptr) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
            AllocConsole();
        }
        hCon = GetStdHandle(STD_OUTPUT_HANDLE);
    }

    // 无法获取控制台句柄时退回到 OutputDebugString
    if (!hCon || hCon == INVALID_HANDLE_VALUE) {
        OutputDebugStringW(helpMsg);
        exit(0);
    }

    // 输出被重定向到文件/管道时无法分页：一次性输出全部内容后退出
    DWORD conMode = 0;
    if (!GetConsoleMode(hCon, &conMode)) {
        WriteTextToHandle(hCon, helpMsg);
        exit(0);
    }

    // 获取控制台窗口尺寸，计算每页行数
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int pageLines = 25;
    if (GetConsoleScreenBufferInfo(hCon, &csbi)) {
        int windowHeight = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        pageLines = std::max(10, windowHeight - 1);
    }

    // 准备标准输入句柄，用于 ReadConsoleInput（同时捕获键盘和鼠标）
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    bool haveStdin = (hStdin != INVALID_HANDLE_VALUE && hStdin != nullptr);
    DWORD prevInputMode = 0;
    if (haveStdin) {
        GetConsoleMode(hStdin, &prevInputMode);
        // 启用鼠标输入，并禁用快速编辑模式防止鼠标被控制台截获
        SetConsoleMode(hStdin, ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS);
    }

    // ======== 等待用户输入的辅助函数 ========
    // 返回值：>0 = 要继续打印的行数，0 = 退出，-1 = 出错(按默认一页)
    auto WaitForInput = [&]() -> int {
        if (!haveStdin) {
            _getwch();
            return pageLines;
        }

        INPUT_RECORD ir{};
        DWORD readCount = 0;

        // 循环读取输入事件，忽略不相关的事件（如鼠标移动）
        while (true) {
            if (!ReadConsoleInput(hStdin, &ir, 1, &readCount) || readCount == 0) {
                return pageLines; // 出错时按默认一页继续
            }

            switch (ir.EventType) {
            case KEY_EVENT:
                if (ir.Event.KeyEvent.bKeyDown) {
                    wchar_t ch = ir.Event.KeyEvent.uChar.UnicodeChar;
                    if (ch == L'Q' || ch == L'q' || ch == 27) {
                        return 0; // 退出
                    }
                    return pageLines; // 任意键 → 一页
                }
                break;

            case MOUSE_EVENT: {
                auto& mouse = ir.Event.MouseEvent;

                if (mouse.dwEventFlags & MOUSE_WHEELED) {
                    // 鼠标滚轮：HIWORD(dwButtonState) 包含滚动增量
                    //  delta > 0 → 滚轮向前/远离用户 → 后退 → 前进行数少
                    //  delta < 0 → 滚轮向后/拉向用户 → 前进 → 前进行数多
                    short delta = HIWORD(mouse.dwButtonState);
                    return (delta > 0) ? 1 : 4; // 向前滚1行，向后滚4行
                }

                if (mouse.dwEventFlags == 0) {
                    // 按钮点击（非移动、非滚轮）
                    if (mouse.dwButtonState & (FROM_LEFT_1ST_BUTTON_PRESSED | RIGHTMOST_BUTTON_PRESSED)) {
                        return pageLines; // 左键/右键点击 → 一页
                    }
                }
                break;
            }
            }
            // 其他事件（鼠标移动等）忽略，继续等待
        }
    };

    // ======== 主输出循环 ========
    std::wstringstream stream(helpMsg);
    std::wstring line;
    int lineCount = 0;
    int nextPauseLine = pageLines; // 到第几行时暂停
    DWORD written;

    while (std::getline(stream, line)) {
        line += L"\n";
        WriteConsoleW(hCon, line.c_str(), (DWORD)line.length(), &written, nullptr);
        lineCount++;

        if (lineCount >= nextPauseLine && !stream.eof()) {
            // 显示提示
            static const wchar_t prompt[] =
                L"--- 滚轮浏览，按任意键翻页，按 Q 退出 ---";
            WriteConsoleW(hCon, prompt,
                (DWORD)(sizeof(prompt) / sizeof(wchar_t) - 1), &written, nullptr);

            int advance = WaitForInput(); // 等待用户操作

            // 清除提示行
            WriteConsoleW(hCon, L"\r", 1, &written, nullptr);
            static const wchar_t spaces[] = L"                                      ";
            WriteConsoleW(hCon, spaces,
                (DWORD)(sizeof(spaces) / sizeof(wchar_t) - 1), &written, nullptr);
            WriteConsoleW(hCon, L"\r", 1, &written, nullptr);

            if (advance <= 0) break; // 用户退出

            // 下次暂停位置 = 当前行 + 用户指定的前进行数
            nextPauseLine = lineCount + advance;
        }
    }

    // 恢复输入模式
    if (haveStdin) {
        SetConsoleMode(hStdin, prevInputMode);
    }

    exit(0);
}

// ==================== 原有函数实现 ====================

int GetDisplayTime(SYSTEMTIME& st) {
    if (g_hasNetworkTime) {
        ULONGLONG elapsedMs = GetTickCount64() - g_syncBaseTick;
        FILETIME ft;
        SystemTimeToFileTime(&g_networkTime, &ft);
        ULARGE_INTEGER uli;
        uli.LowPart = ft.dwLowDateTime;
        uli.HighPart = ft.dwHighDateTime;
        uli.QuadPart += elapsedMs * 10000ULL;
        ft.dwLowDateTime = uli.LowPart;
        ft.dwHighDateTime = uli.HighPart;
        FileTimeToSystemTime(&ft, &st);
        return 1;
    }
    GetLocalTime(&st);
    return 0;
}

std::wstring GetTimeSourceString() {
    // 保留原有图标：🌐 网络时间 / 💻 本地时间
    std::wstring text = g_hasNetworkTime ? L"🌐 网络时间" : L"💻 本地时间";
    // 仅当按钮栏未显示（已自动隐藏或 --no-button）时，
    // 才在来源文字后追加“同步中”；按钮可见时由“同步”按钮文本体现同步状态
    if (g_syncing && (g_hideButtons || !g_buttonsVisible)) text += L" 同步中";
    return text;
}

std::wstring GetTimeDateStringEx() {
    SYSTEMTIME st;
    GetDisplayTime(st);

    wchar_t timeStr[64];
    StringCchPrintfW(timeStr, _countof(timeStr), L"%02d:%02d:%02d\n%04d-%02d-%02d",
        st.wHour, st.wMinute, st.wSecond,
        st.wYear, st.wMonth, st.wDay);

    const wchar_t* weekdays[] = {
        L"星期日", L"星期一", L"星期二", L"星期三",
        L"星期四", L"星期五", L"星期六"
    };

    std::wstring result = timeStr;
    result += L"\n";
    result += weekdays[st.wDayOfWeek];
    return result;
}

std::wstring GetTimeDateString() {
    return GetTimeDateStringEx();
}

// 创建按钮控件（仅创建一次，不设定位置/大小，后续由 UpdateButtonLayout 统一布局）
void CreateButtons(HWND hWnd) {
    if (g_hideButtons) return;
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE);

    hFullscreenBtn = CreateWindowW(L"BUTTON", g_noHotKeys ? L"全屏" : L"全屏 (F11)",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        0, 0, 0, 0, hWnd, (HMENU)ID_FULLSCREEN_BTN, hInst, NULL);

    hExitBtn = CreateWindowW(L"BUTTON", g_noHotKeys ? L"退出" : L"退出 (Esc)",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        0, 0, 0, 0, hWnd, (HMENU)ID_EXIT_BTN, hInst, NULL);

    hSyncBtn = CreateWindowW(L"BUTTON", g_noHotKeys ? L"同步时间" : L"同步 (F5)",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        0, 0, 0, 0, hWnd, (HMENU)ID_SYNC_BTN, hInst, NULL);

    hTopmostBtn = CreateWindowW(L"BUTTON", g_topmost ? (g_noHotKeys ? L"取消置顶" : L"消顶 (F6)") : (g_noHotKeys ? L"置顶" : L"置顶 (F6)"),
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        0, 0, 0, 0, hWnd, (HMENU)ID_TOPMOST_BTN, hInst, NULL);
}

// 根据窗口大小动态计算按钮宽高、位置、间距，并刷新字体（基于 DPI 缩放上限）
void UpdateButtonLayout(HWND hWnd) {
    if (g_hideButtons) return;
    if (!hFullscreenBtn || !hExitBtn || !hSyncBtn || !hTopmostBtn) return;

    RECT clientRect;
    GetClientRect(hWnd, &clientRect);
    int clientWidth = clientRect.right - clientRect.left;
    int clientHeight = clientRect.bottom - clientRect.top;
    if (clientWidth <= 0 || clientHeight <= 0) return;

    // 基于 DPI 动态计算最小/最大按钮尺寸
    int minBtnWidth = ScaleByDpi(BASE_MIN_BTN_WIDTH, hWnd);
    int maxBtnWidth = ScaleByDpi(BASE_MAX_BTN_WIDTH, hWnd);
    int minBtnHeight = ScaleByDpi(BASE_MIN_BTN_HEIGHT, hWnd);
    int maxBtnHeight = ScaleByDpi(BASE_MAX_BTN_HEIGHT, hWnd);

    int newBtnWidth = std::max(minBtnWidth, std::min(maxBtnWidth, clientWidth / 7));
    int newBtnHeight = std::max(minBtnHeight, std::min(maxBtnHeight, clientHeight / 12));

    int spacing = std::max(8, (int)(newBtnWidth * 0.2f));
    int marginBottom = std::max(15, (int)(clientHeight * 0.03f));

    int totalWidth = newBtnWidth * 4 + spacing * 3;
    int startX = std::max(0, (clientWidth - totalWidth) / 2);
    int btnY = clientHeight - newBtnHeight - marginBottom;

    g_buttonWidth = newBtnWidth;
    g_buttonHeight = newBtnHeight;

    // 使用 BeginDeferWindowPos / EndDeferWindowPos 原子批量更新按钮位置，
    // 四个按钮在单次刷新中同时移动到位，消除逐个 SetWindowPos 的中间态闪烁。
    // 注意：本程序未使用 WS_EX_COMPOSITED（与 D2D HWND RenderTarget 不兼容）。
    HDWP hdwp = BeginDeferWindowPos(4);
    if (hdwp) {
        DeferWindowPos(hdwp, hFullscreenBtn, NULL,
            startX, btnY, newBtnWidth, newBtnHeight,
            SWP_NOZORDER | SWP_NOACTIVATE);
        DeferWindowPos(hdwp, hExitBtn, NULL,
            startX + newBtnWidth + spacing, btnY, newBtnWidth, newBtnHeight,
            SWP_NOZORDER | SWP_NOACTIVATE);
        DeferWindowPos(hdwp, hSyncBtn, NULL,
            startX + (newBtnWidth + spacing) * 2, btnY, newBtnWidth, newBtnHeight,
            SWP_NOZORDER | SWP_NOACTIVATE);
        DeferWindowPos(hdwp, hTopmostBtn, NULL,
            startX + (newBtnWidth + spacing) * 3, btnY, newBtnWidth, newBtnHeight,
            SWP_NOZORDER | SWP_NOACTIVATE);
        EndDeferWindowPos(hdwp);
    }

    // 统一刷新按钮
    InvalidateRect(hFullscreenBtn, NULL, TRUE);
    InvalidateRect(hExitBtn,       NULL, TRUE);
    InvalidateRect(hSyncBtn,       NULL, TRUE);
    InvalidateRect(hTopmostBtn,    NULL, TRUE);

    // 实时更新字体大小：CreateDynamicFonts 内部拥有字体尺寸缓存，
    // 尺寸未变时直接返回，不产生实际开销，频繁调用也不会闪烁。
    CreateDynamicFonts(hWnd);
}

void CreateDynamicFonts(HWND hWnd) {
    FontSizes sizes = CalculateFontSizes(hWnd);

    // 提前计算按钮字体尺寸，用于缓存判断
    int minBtnFont = ScaleByDpi(BASE_MIN_BTN_FONT, hWnd);
    int btnFontSize = std::max(minBtnFont, (int)(g_buttonHeight * 0.4));

    // === 字体尺寸缓存：仅当尺寸真正变化时才重建字体，避免无用 DeleteObject/CreateFont ===
    static FontSizes lastSizes = { 0 };
    static int lastBtnFontSize = 0;
    if (lastSizes.time == sizes.time && lastSizes.date == sizes.date &&
        lastSizes.week == sizes.week && lastSizes.source == sizes.source &&
        lastBtnFontSize == btnFontSize) {
        return; // 所有字体尺寸均未变化，跳过重建
    }
    lastSizes = sizes;
    lastBtnFontSize = btnFontSize;

    // 确定最终使用的字体族名称：
    //   1. 命令行指定字体（--font-name / --font-path）
    //   2. 嵌入字体（Source Han Sans Regular）
    //   3. 回退字体（Segoe UI）
    const wchar_t* fontFamily = !g_fontFamilyToUse.empty() ? g_fontFamilyToUse.c_str() :
        (g_useEmbeddedFont ? FONT_FAMILY_EMBEDDED : FONT_FAMILY_FALLBACK);

    HFONT hNewLarge = CreateFontW(
        -sizes.time, 0, 0, 0, FW_BOLD,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, fontFamily
    );
    HFONT hNewDate = CreateFontW(
        -sizes.date, 0, 0, 0, FW_SEMIBOLD,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, fontFamily
    );
    HFONT hNewSource = CreateFontW(
        -sizes.source, 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, fontFamily
    );

    if (hNewLarge) {
        if (hLargeFont) DeleteObject(hLargeFont);
        hLargeFont = hNewLarge;
    }
    if (hNewDate) {
        if (hDateFont) DeleteObject(hDateFont);
        hDateFont = hNewDate;
    }
    if (hNewSource) {
        if (hSourceFont) DeleteObject(hSourceFont);
        hSourceFont = hNewSource;
    }

    if (hFullscreenBtn && hExitBtn && hSyncBtn && hTopmostBtn && g_buttonHeight > 0) {
        HFONT hBtnFont = CreateFontW(
            -btnFontSize, 0, 0, 0, FW_MEDIUM,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, fontFamily
        );
        if (hBtnFont) {
            SendMessageW(hFullscreenBtn, WM_SETFONT, (WPARAM)hBtnFont, FALSE);
            SendMessageW(hExitBtn,       WM_SETFONT, (WPARAM)hBtnFont, FALSE);
            SendMessageW(hSyncBtn,       WM_SETFONT, (WPARAM)hBtnFont, FALSE);
            SendMessageW(hTopmostBtn,    WM_SETFONT, (WPARAM)hBtnFont, FALSE);
            if (hNormalFont) DeleteObject(hNormalFont);
            hNormalFont = hBtnFont;
        }
    }
}

void HandleTouchInput(HWND hWnd, WPARAM wParam, LPARAM lParam) {
    UINT cInputs = LOWORD(wParam);
    PTOUCHINPUT pInputs = new (std::nothrow) TOUCHINPUT[cInputs];
    if (!pInputs) {
        CloseTouchInputHandle((HTOUCHINPUT)lParam);
        return;
    }
    if (GetTouchInputInfo((HTOUCHINPUT)lParam, cInputs, pInputs, sizeof(TOUCHINPUT))) {
        POINT ptInput;
        for (UINT i = 0; i < cInputs; i++) {
            TOUCHINPUT ti = pInputs[i];
            ptInput.x = TOUCH_COORD_TO_PIXEL(ti.x);
            ptInput.y = TOUCH_COORD_TO_PIXEL(ti.y);
            ScreenToClient(hWnd, &ptInput);
            if (ti.dwFlags & TOUCHEVENTF_DOWN) {
                if (!g_hideButtons) {
                    ShowButtons(hWnd);
                    ResetInterActionTimer();
                }
                static ULONGLONG lastTouchTime = 0;
                static POINT lastTouchPoint = { 0, 0 };
                ULONGLONG currentTime = GetTickCount64();
                POINT currentPoint = ptInput;
                if ((currentTime - lastTouchTime) < 500 &&
                    abs(currentPoint.x - lastTouchPoint.x) < 50 &&
                    abs(currentPoint.y - lastTouchPoint.y) < 50) {
                    ToggleFullscreen(hWnd);
                    if (hFullscreenBtn)
                        SetWindowTextW(hFullscreenBtn, IsFullscreen() ? L"窗口化 (F11)" : L"全屏 (F11)");
                }
                lastTouchTime = currentTime;
                lastTouchPoint = currentPoint;
            }
        }
        CloseTouchInputHandle((HTOUCHINPUT)lParam);
    }
    delete[] pInputs;
}

void StartSync(HWND hWnd) {
    if (g_syncing) return;
    g_syncing = true;
    // 同步时不再强制弹出按钮栏：按钮已隐藏时保持隐藏，
    // 避免自动同步（每小时）把按钮栏唤出。
    // 左上角时间来源文字后追加“同步中”，立即重绘使其可见。
    InvalidateRect(hWnd, NULL, FALSE);
    if (hSyncBtn) SetWindowTextW(hSyncBtn, L"同步中...");
    ResetCancel();
    if (g_syncThread.joinable()) {
        CancelPendingOperations();
        g_syncThread.join();
    }
    g_syncThread = std::thread(SyncThreadProc, hWnd);
}

void SyncThreadProc(HWND hWnd) {
    bool success = SyncTimeOnce();
    if (!g_exitFlag) {
        PostMessageW(hWnd, WM_NTP_SYNC_FINISHED, success ? 1 : 0, 0);
    }
}

FontSizes CalculateFontSizes(HWND hWnd) {
    RECT clientRect;
    GetClientRect(hWnd, &clientRect);
    int windowHeight = clientRect.bottom - clientRect.top;

    FontSizes sizes;
    int targetTimeHeight = (int)(windowHeight * 0.35);
    sizes.time = (int)(targetTimeHeight * 0.75);
    int minTimeFont = ScaleByDpi(BASE_MIN_TIME_FONT, hWnd);
    sizes.time = std::max(minTimeFont, std::min(sizes.time, (int)(windowHeight * 0.45)));

    sizes.date = (int)(sizes.time * 0.3);
    int minDateFont = ScaleByDpi(BASE_MIN_DATE_FONT, hWnd);
    sizes.date = std::max(minDateFont, std::min(sizes.date, (int)(windowHeight * 0.12)));

    sizes.week = (int)(sizes.time * 0.3);
    int minWeekFont = ScaleByDpi(BASE_MIN_WEEK_FONT, hWnd);
    sizes.week = std::max(minWeekFont, std::min(sizes.week, (int)(windowHeight * 0.1)));

    // 来源字体：基准范围 10~26，随 DPI 缩放
    int baseSourceMin = 10;
    int baseSourceMax = 16;
    int minSourceFont = ScaleByDpi(baseSourceMin, hWnd);
    int maxSourceFont = ScaleByDpi(baseSourceMax, hWnd);
    int rawSource = (int)(sizes.time * 0.25);
    sizes.source = std::max(minSourceFont, std::min(maxSourceFont, rawSource));

    return sizes;
}

static const wchar_t* GetActiveFontFamily() {
    if (!g_fontFamilyToUse.empty()) return g_fontFamilyToUse.c_str();
    if (g_useEmbeddedFont) return FONT_FAMILY_EMBEDDED;
    return FONT_FAMILY_FALLBACK;
}

// 检查字符串是否为纯数字（允许可选的负号开头）
static bool IsNumericString(const wchar_t* str) {
    if (!str || *str == L'\0') return false;
    const wchar_t* p = str;
    if (*p == L'-') ++p; // 允许负号开头，用于检测负数
    if (*p == L'\0') return false; // 只有负号不算有效数字
    while (*p) {
        if (*p < L'0' || *p > L'9') return false;
        ++p;
    }
    return true;
}

void ToggleTopmost(HWND hWnd) {
    g_topmost = !g_topmost;
    LONG exStyle = GetWindowLong(hWnd, GWL_EXSTYLE);
    if (g_topmost) {
        SetWindowLong(hWnd, GWL_EXSTYLE, exStyle | WS_EX_TOPMOST);
        SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }
    else {
        SetWindowLong(hWnd, GWL_EXSTYLE, exStyle & ~WS_EX_TOPMOST);
        SetWindowPos(hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }
    if (hTopmostBtn) {
        SetWindowTextW(hTopmostBtn, g_topmost ?
            (g_noHotKeys ? L"取消置顶" : L"消顶 (F6)") :
            (g_noHotKeys ? L"置顶" : L"置顶 (F6)"));
    }
}

// ===== 按钮自动隐藏功能 =====

void ShowButtons(HWND hWnd) {
    if (g_hideButtons) return;
    if (!hFullscreenBtn || !hExitBtn || !hSyncBtn || !hTopmostBtn) return;
    if (!g_buttonsVisible) {
        HWND btns[] = { hFullscreenBtn, hExitBtn, hSyncBtn, hTopmostBtn };
        if (g_noAnim) {
            for (HWND btn : btns) ShowWindow(btn, SW_SHOW);
        } else {
            for (HWND btn : btns) {
                if (!AnimateWindow(btn, 200, AW_BLEND))
                    ShowWindow(btn, SW_SHOW);
            }
        }
        g_buttonsVisible = true;
        InvalidateRect(hWnd, NULL, FALSE); // 按钮已显示，立即移除左上角“同步中”
    }
}

void HideButtons(HWND hWnd) {
    if (g_hideButtons) return;
    if (!hFullscreenBtn || !hExitBtn || !hSyncBtn || !hTopmostBtn) return;
    if (g_buttonsVisible) {
        HWND btns[] = { hFullscreenBtn, hExitBtn, hSyncBtn, hTopmostBtn };
        if (g_noAnim) {
            for (HWND btn : btns) ShowWindow(btn, SW_HIDE);
        } else {
            for (HWND btn : btns) {
                if (!AnimateWindow(btn, 200, AW_BLEND | AW_HIDE))
                    ShowWindow(btn, SW_HIDE);
            }
        }
        g_buttonsVisible = false;
        InvalidateRect(hWnd, NULL, FALSE); // 按钮已隐藏，若同步中则立即在左上角显示“同步中”
    }
}

void ResetInterActionTimer() {
    g_lastInteractionTime = GetTickCount64();
}

void CheckAutoHide(HWND hWnd) {
    if (g_hideButtons || !g_buttonsVisible || g_noAutoHide) return;
    if (g_manualShowActive) {
        if (GetTickCount64() - g_manualShowStartTime >= AUTO_HIDE_MANUAL_MS) {
            HideButtons(hWnd);
            g_manualShowActive = false;
            ResetInterActionTimer();
        }
        return;
    }
    if (GetTickCount64() - g_lastInteractionTime >= AUTO_HIDE_IDLE_MS) {
        HideButtons(hWnd);
    }
}

// ===== 嵌入字体加载 =====

// 字体枚举回调数据结构（用于 IsFontNameValid）
struct FontCheckData {
    const wchar_t* targetName;
    bool found;
};

// 字体枚举回调函数
static int CALLBACK EnumFontCheckProc(const LOGFONTW* lpelfe, const TEXTMETRICW* lpntme, DWORD FontType, LPARAM lParam) {
    auto* data = reinterpret_cast<FontCheckData*>(lParam);
    if (data->targetName && data->targetName[0] &&
        _wcsicmp(lpelfe->lfFaceName, data->targetName) == 0) {
        data->found = true;
        return 0; // 找到目标字体，停止枚举
    }
    return 1; // 继续枚举
}

// 验证字体名称是否有效（系统中是否存在该字体族）
static bool IsFontNameValid(const wchar_t* fontName) {
    if (!fontName || fontName[0] == L'\0') return false;

    HDC hdc = GetDC(nullptr);
    if (!hdc) return false;

    LOGFONTW lf = { 0 };
    lf.lfCharSet = DEFAULT_CHARSET;

    FontCheckData data = { fontName, false };
    EnumFontFamiliesExW(hdc, &lf, EnumFontCheckProc, (LPARAM)&data, 0);

    ReleaseDC(nullptr, hdc);
    return data.found;
}

// 验证字体文件路径是否存在且可读
static bool IsFontFileReadable(const wchar_t* fontPath) {
    if (!fontPath || fontPath[0] == L'\0') return false;

    DWORD attrs = GetFileAttributesW(fontPath);
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) return false;

    return true;
}

// 前向声明：ReportError 用于在 GUI 启动时将参数错误同时弹窗显示
static void ReportError(const wchar_t* msg);

// 从字体文件路径中提取文件名（不含扩展名）作为字体族名称的猜测值
static std::wstring GuessFontNameFromPath(const std::wstring& fontPath) {
    size_t sep = fontPath.find_last_of(L"\\/");
    std::wstring fname = (sep != std::wstring::npos) ? fontPath.substr(sep + 1) : fontPath;
    size_t dot = fname.find_last_of(L".");
    return (dot != std::wstring::npos) ? fname.substr(0, dot) : fname;
}

// 从文件路径加载字体（私有方式，仅当前进程可见）
static bool LoadFontFromPath(const std::wstring& fontPath) {
    int added = AddFontResourceExW(fontPath.c_str(), FR_PRIVATE, 0);
    if (added == 0) {
        wchar_t errMsg[512];
        StringCchPrintfW(errMsg, _countof(errMsg), L"错误: 无法加载字体文件 '%s'，文件可能不是有效的字体格式或已被损坏\n", fontPath.c_str());
        ReportError(errMsg);
        return false;
    }
    g_fontLoadedFromPath = true;
    return true;
}

static bool LoadEmbeddedFont() {
    HRSRC hRes = FindResourceW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDF_SOURCEHANS), RT_RCDATA);
    if (!hRes) return false;

    HGLOBAL hData = LoadResource(GetModuleHandleW(nullptr), hRes);
    if (!hData) return false;

    void* pFontData = LockResource(hData);
    DWORD dwSize = SizeofResource(GetModuleHandleW(nullptr), hRes);
    if (!pFontData || dwSize == 0) return false;

    DWORD cFonts = 0;
    g_hEmbeddedFont = AddFontMemResourceEx(pFontData, dwSize, 0, &cFonts);
    if (!g_hEmbeddedFont || cFonts == 0) return false;

    return true;
}

// 参数错误/警告的弹窗策略：
// - 控制台启动：仅 --show-warning 时弹窗；
// - 双击/快捷方式启动：默认弹窗（-w/--ignore-warning 抑制）；
// - 其它环境（无法判定来源）：一律弹窗。
static bool ShouldShowDialog() {
    return g_showWarning
        || (g_launchEnv == LaunchEnv::Gui && !g_ignoreWarning)
        || g_launchEnv == LaunchEnv::Other;
}

// 报告参数错误：
// - 控制台启动：输出到标准错误（不弹窗，--show-warning 可强制弹窗）；
// - 双击/快捷方式启动：弹窗（-w 可抑制），不再先弹出控制台窗口；
// - 其它启动环境：标准错误 + 弹窗。
static void ReportError(const wchar_t* msg) {
    WriteToStderr(msg);
    if (ShouldShowDialog()) {
        MessageBoxW(nullptr, msg, L"TimeDisplay - 参数错误", MB_OK | MB_ICONERROR);
    }
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow) {

    // 检测启动环境：
    // - 有父控制台 → 命令行（cmd/powershell）启动；
    // - 无父控制台 → 快捷方式/双击等 GUI 启动；
    // - 其它失败原因 → 无法判定来源的其它环境。
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        // 有父控制台，来自命令行
        FreeConsole();
        g_launchEnv = LaunchEnv::Console;
    }
    else {
        DWORD attachErr = GetLastError();
        if (attachErr == ERROR_INVALID_HANDLE) {
            // 无父控制台，可能来自快捷方式或直接双击
            g_launchEnv = LaunchEnv::Gui;
        }
        else if (attachErr == ERROR_ACCESS_DENIED) {
            // 已附加到某个控制台（被其它程序调起并预先附加）
            g_launchEnv = LaunchEnv::Console;
        }
        else {
            // 其它环境：无法判定来源
            g_launchEnv = LaunchEnv::Other;
        }
    }

    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        // 预扫描 --debug（可带日志路径）：尽早启用日志，让后续所有输出都能记录
        for (int i = 1; i < argc; ++i) {
            if (wcscmp(argv[i], L"--debug") == 0) {
                const wchar_t* logPath = nullptr;
                if (i + 1 < argc && argv[i + 1][0] != L'-') {
                    logPath = argv[i + 1];
                }
                if (DebugLogEnable(logPath)) {
                    LOGI(L"命令行: %s", GetCommandLineW());
                    LOGI(L"启动环境: %s", g_launchEnv == LaunchEnv::Console ? L"控制台" :
                        (g_launchEnv == LaunchEnv::Gui ? L"快捷方式/双击" : L"其它"));
                }
                else {
                    ReportError(L"错误: 无法创建调试日志文件，日志功能不可用\n");
                }
                break;
            }
        }
        // 预扫描警告开关：保证它们位于错误参数之前或之后都能生效
        for (int i = 1; i < argc; ++i) {
            if (wcscmp(argv[i], L"--show-warning") == 0) {
                g_showWarning = true;
            }
            else if (wcscmp(argv[i], L"--ignore-warning") == 0 || wcscmp(argv[i], L"-w") == 0) {
                g_ignoreWarning = true;
            }
        }
        for (int i = 1; i < argc; ++i) {
            if (wcscmp(argv[i], L"--no-button") == 0 || wcscmp(argv[i], L"-b") == 0) {
                g_hideButtons = true;
            }
            else if (wcscmp(argv[i], L"--full-screen") == 0 || wcscmp(argv[i], L"-f") == 0) {
                g_startFullscreen = true;
            }
            else if (wcscmp(argv[i], L"--nok-key") == 0 || wcscmp(argv[i], L"-k") == 0) {
                g_noHotKeys = true;
            }
            else if (wcscmp(argv[i], L"--topmost") == 0 || wcscmp(argv[i], L"-p") == 0) {
                g_startTopmost = true;
            }
            else if (wcscmp(argv[i], L"--help") == 0 || wcscmp(argv[i], L"-h") == 0) {
                g_givehelp = true;
            }
            else if (wcscmp(argv[i], L"--version") == 0 || wcscmp(argv[i], L"-v") == 0) {
                g_showVersion = true;
            }
            else if (wcscmp(argv[i], L"--transparency") == 0 || wcscmp(argv[i], L"-t") == 0) {
                // --transparency/-t 需要后面跟数值参数
                if (i + 1 >= argc) {
                    ReportError(L"错误: 选项 --transparency/-t 需要一个数值参数 (0-255)\n"
                                L"用法示例: TimeDisplay.exe --transparency 200\n");
                    LocalFree(argv);
                    return 1;
                }
                const wchar_t* valStr = argv[++i];
                if (!IsNumericString(valStr)) {
                    wchar_t errMsg[256];
                    StringCchPrintfW(errMsg, _countof(errMsg), L"错误: --transparency/-t 的参数 '%s' 不是有效的数字\n", valStr);
                    ReportError(errMsg);
                    LocalFree(argv);
                    return 1;
                }
                int val = _wtoi(valStr);
                if (val < 0 || val > 255) {
                    wchar_t errMsg[256];
                    StringCchPrintfW(errMsg, _countof(errMsg), L"错误: 透明度值 %d 超出有效范围，允许值为 0-255\n", val);
                    ReportError(errMsg);
                    LocalFree(argv);
                    return 1;
                }
                if (val == 0) {
                    static const wchar_t warnMsg[] =
                        L"警告: 透明度设置为 0，窗口将完全不可见。\n"
                        L"提示: 若确实需要此设置，请确认这是您的预期行为。\n";
                    WriteToStderr(warnMsg);
                    if (ShouldShowDialog()) {
                        MessageBoxW(nullptr, warnMsg, L"TimeDisplay - 参数警告",
                            MB_OK | MB_ICONWARNING);
                    }
                }
                g_transparency = val;
            }
            else if (wcscmp(argv[i], L"--font-name") == 0) {
                // --font-name 需要后面跟字体名称参数
                if (i + 1 >= argc) {
                    ReportError(L"错误: 选项 --font-name 需要一个字体族名称参数\n"
                                L"用法示例: TimeDisplay.exe --font-name \"Arial\"\n");
                    LocalFree(argv);
                    return 1;
                }
                g_cmdFontName = argv[++i];
                if (g_cmdFontName.empty()) {
                    ReportError(L"错误: --font-name 的字体名称不能为空\n");
                    LocalFree(argv);
                    return 1;
                }
            }
            else if (wcscmp(argv[i], L"--font-path") == 0) {
                // --font-path 需要后面跟字体文件路径参数
                if (i + 1 >= argc) {
                    ReportError(L"错误: 选项 --font-path 需要一个字体文件路径参数\n"
                                L"用法示例: TimeDisplay.exe --font-path \"C:\\Windows\\Fonts\\myfont.ttf\"\n");
                    LocalFree(argv);
                    return 1;
                }
                g_cmdFontPath = argv[++i];
                if (g_cmdFontPath.empty()) {
                    ReportError(L"错误: --font-path 的字体路径不能为空\n");
                    LocalFree(argv);
                    return 1;
                }
            }
            else if (wcscmp(argv[i], L"--show-warning") == 0) {
                g_showWarning = true;
            }
            else if (wcscmp(argv[i], L"--ignore-warning") == 0 || wcscmp(argv[i], L"-w") == 0) {
                g_ignoreWarning = true;
            }
            else if (wcscmp(argv[i], L"--force-gdi") == 0 || wcscmp(argv[i], L"--disable-gpu") == 0) {
                g_forceGDI = true;
            }
            else if (wcscmp(argv[i], L"--no-auto-hide") == 0) {
                g_noAutoHide = true;
            }
            else if (wcscmp(argv[i], L"--no-animation") == 0) {
                g_noAnim = true;
            }
            else if (wcscmp(argv[i], L"--disable-touch") == 0) {
                g_disableTouch = true;
            }
            else if (wcscmp(argv[i], L"--debug") == 0) {
                // 日志已在预扫描中启用，这里只消费可选的日志路径参数
                if (i + 1 < argc && argv[i + 1][0] != L'-') {
                    ++i;
                }
            }
            else {
                // 无法识别的参数 → 报错并退出
                wchar_t errMsg[384];
                StringCchPrintfW(errMsg, _countof(errMsg), L"错误: 无法识别的选项 '%s'\n使用 --help/-h 查看所有可用选项\n", argv[i]);
                ReportError(errMsg);
                LocalFree(argv);
                return 1;
            }
        }
        LocalFree(argv);
    }

    // 记录解析后的启动参数（详细日志仅 Debug 构建输出）
    LOGV(L"参数: 透明度=%d, 隐藏按钮=%d, 全屏=%d, 置顶=%d, 禁用热键=%d, 强制GDI=%d, 自动隐藏=%d, 动画=%d, 触摸=%d",
        g_transparency, g_hideButtons, g_startFullscreen, g_startTopmost, g_noHotKeys,
        g_forceGDI, !g_noAutoHide, !g_noAnim, !g_disableTouch);

    // ===== 帮助或版本信息优先处理，不创建窗口 =====
    if (g_showVersion) {
        ShowVersionAndExit();
    }
    if (g_givehelp) {
        ShowHelpAndExit();
    }

    // ===== 字体参数验证 =====
    // 验证 --font-name（未同时提供 --font-path 时）：检查系统中是否存在该字体
    if (!g_cmdFontName.empty() && g_cmdFontPath.empty()) {
        if (!IsFontNameValid(g_cmdFontName.c_str())) {
            std::wstring errMsg = L"错误: 系统中未找到字体 '" + g_cmdFontName + L"'，请检查字体名称是否正确\n";
            errMsg += L"提示: 使用 --font-path 可以指定外部字体文件路径\n";
            ReportError(errMsg.c_str());
            return 1;
        }
    }

    // 验证 --font-path：检查文件是否存在且可读
    if (!g_cmdFontPath.empty()) {
        if (!IsFontFileReadable(g_cmdFontPath.c_str())) {
            std::wstring errMsg = L"错误: 字体文件不存在或无法访问 '" + g_cmdFontPath + L"'\n";
            ReportError(errMsg.c_str());
            return 1;
        }
    }

    // ===== 字体解析：确定最终使用的字体 =====
    if (!g_cmdFontPath.empty()) {
        // 从文件加载字体
        if (!LoadFontFromPath(g_cmdFontPath)) {
            return 1; // LoadFontFromPath 内部已输出错误信息
        }
        LOGI(L"字体: 已从文件加载 '%s'", g_cmdFontPath.c_str());
        // 如果用户未指定 --font-name，从文件名猜测字体族名称
        if (g_cmdFontName.empty()) {
            g_fontFamilyToUse = GuessFontNameFromPath(g_cmdFontPath);
            wchar_t logMsg[512];
            StringCchPrintfW(logMsg, _countof(logMsg), L"从文件名推断字体族名称: %s\n", g_fontFamilyToUse.c_str());
            WriteToStderr(logMsg);
        } else {
            g_fontFamilyToUse = g_cmdFontName;
        }
    } else if (!g_cmdFontName.empty()) {
        // 仅使用 --font-name，指定系统字体
        g_fontFamilyToUse = g_cmdFontName;
    }

    // ===== 高DPI设置 =====
    // user32.dll 在进程启动时已加载，用 GetModuleHandleW 获取句柄，
    // 避免 LoadLibraryW 额外的引用计数（也无需 FreeLibrary）。
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        auto pSetProcessDpiAwarenessContext = (BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT))GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pSetProcessDpiAwarenessContext) {
            pSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            LOGV(L"DPI: Per-Monitor V2");
        } else {
            // Windows 7：没有 SetProcessDpiAwarenessContext，回退到系统级 DPI 感知
            SetProcessDPIAware();
            LOGV(L"DPI: System Aware（Win7 回退）");
        }
    }
    else {
        SetProcessDPIAware();
        LOGV(L"DPI: System Aware（user32 句柄获取失败回退）");
    }

    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    const wchar_t CLASS_NAME[] = L"Windows11StyleClockClass";
    UnregisterClassW(CLASS_NAME, hInstance);

    WNDCLASSEXW wcex = { 0 };
    wcex.cbSize = sizeof(wcex);
    wcex.style = 0;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = nullptr;  // 不需要系统擦除背景，全部在 WM_PAINT 中绘制
    wcex.lpszClassName = CLASS_NAME;
    wcex.hIcon = LoadIconW(hInstance, MAKEINTRESOURCE(101));
    wcex.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCE(101));

    if (!RegisterClassExW(&wcex)) {
        DWORD err = GetLastError();
        wchar_t msg[256];
        StringCchPrintfW(msg, _countof(msg), L"窗口类注册失败！\n错误码: %lu\ncbSize: %u", err, wcex.cbSize);
        LOGI(L"错误: 窗口类注册失败 (错误码 %lu)", err);
        MessageBoxW(nullptr, msg, L"错误", MB_ICONERROR);
        return 1;
    }

    // 获取主显示器 DPI 用于初始化窗口大小
    UINT dpi = 96;
    HDC hdcScreen = GetDC(nullptr);
    if (hdcScreen) {
        dpi = GetDeviceCaps(hdcScreen, LOGPIXELSX);
        ReleaseDC(nullptr, hdcScreen);
    }
    // 基准初始大小 (96 DPI 下)
    const int BASE_INIT_WIDTH = 800;
    const int BASE_INIT_HEIGHT = 600;
    int initWidth = (int)(BASE_INIT_WIDTH * dpi / 96.0f);
    int initHeight = (int)(BASE_INIT_HEIGHT * dpi / 96.0f);

    // 注意：不使用 WS_EX_COMPOSITED，因为它与 D2D HWND RenderTarget 不兼容
    // （D2D 使用自己的 DXGI 交换链双缓冲，GDI 回退也做了内存 DC 双缓冲，两者都已足够）
    DWORD exStyle = WS_EX_APPWINDOW;
    if (g_transparency < 255) {
        exStyle |= WS_EX_LAYERED;
    }
    hMainWindow = CreateWindowExW(
        exStyle,
        CLASS_NAME,
        L"时间显示器",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        initWidth, initHeight,
        nullptr, nullptr, hInstance, nullptr);

    if (!hMainWindow) {
        LOGI(L"错误: 主窗口创建失败 (错误码 %lu)", GetLastError());
        MessageBoxW(nullptr, L"主窗口创建失败！", L"错误", MB_ICONERROR);
        return 1;
    }
    LOGV(L"主窗口已创建: %dx%d (DPI %u)", initWidth, initHeight, dpi);

    if (g_transparency < 255) {
        SetLayeredWindowAttributes(hMainWindow, 0, (BYTE)g_transparency, LWA_ALPHA);
    }

    // Windows 11 圆角效果（仅在 Windows 11+ 上生效，Win7/10 静默忽略）
    DWM_WINDOW_CORNER_PREFERENCE cornerPreference = DWMWCP_ROUND;
    HRESULT hr = DwmSetWindowAttribute(hMainWindow, DWMWA_WINDOW_CORNER_PREFERENCE,
        &cornerPreference, sizeof(cornerPreference));
    if (FAILED(hr)) {
        // Windows 7 / 10 不支持此属性，忽略
    }

    // 注册触摸窗口（Win7+ 支持），失败不阻塞启动
    if (!RegisterTouchWindow(hMainWindow, TWF_FINETOUCH)) {
        LOGV(L"触摸注册失败（当前系统不支持触摸）");
    }

    if (!InitializeNTPClient()) {
        LOGI(L"错误: NTP 客户端初始化失败");
    }

    // 加载嵌入字体（当未通过命令行指定字体时）
    if (g_fontFamilyToUse.empty()) {
        g_useEmbeddedFont = LoadEmbeddedFont();
        if (g_useEmbeddedFont) {
            LOGI(L"字体: 嵌入字体加载成功 (Source Han Sans Regular)");
        } else {
            LOGI(L"字体: 嵌入字体加载失败，回退 Segoe UI");
        }
    } else {
        LOGI(L"字体: 使用命令行指定字体 '%s'", g_fontFamilyToUse.c_str());
    }

    CreateDynamicFonts(hMainWindow);
    InitializeFullscreenModule(hMainWindow);

    // 在窗口可见前初始化 D2D（避免首帧走 GDI 导致按钮闪烁）
    // 如果指定了 --force-gdi / --disable-gpu，则跳过 D2D 初始化，强制使用 GDI 渲染
    bool layeredWindow = (GetWindowLongW(hMainWindow, GWL_EXSTYLE) & WS_EX_LAYERED) != 0;
    if (g_forceGDI) {
        LOGI(L"渲染: 命令行指定 --force-gdi，强制使用 GDI");
    } else if (layeredWindow) {
        // 分层窗口（透明度 < 255）为“系统重定向合成”，D2D HwndRenderTarget 经 DXGI
        // 呈现与 WS_EX_LAYERED 不兼容（参见 MSDN《Layered Windows with Direct2D》）；
        // 强制使用 GDI 双缓冲，避免透明模式下的异常与闪烁。
        LOGI(L"渲染: 检测到分层窗口（透明度模式），强制使用 GDI");
    } else if (!D2DRenderer_Init(hMainWindow)) {
        LOGI(L"渲染: D2D 初始化失败，回退 GDI（详见上方 D2D 日志）");
    } else {
        LOGI(L"渲染: 使用 D2D 渲染");
    }

    if (g_startTopmost) {
        g_topmost = true;
        SetWindowLong(hMainWindow, GWL_EXSTYLE, GetWindowLong(hMainWindow, GWL_EXSTYLE) | WS_EX_TOPMOST);
        SetWindowPos(hMainWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }

    ShowWindow(hMainWindow, nCmdShow);
    UpdateWindow(hMainWindow);

    if (g_startFullscreen) {
        ToggleFullscreen(hMainWindow);
        if (hFullscreenBtn) SetWindowTextW(hFullscreenBtn, IsFullscreen() ? L"窗口化 (F11)" : L"全屏 (F11)");
    }

    StartSync(hMainWindow);
    SetTimer(hMainWindow, TIMER_AUTO_SYNC, AUTO_SYNC_INTERVAL_MS, nullptr);
    SetTimer(hMainWindow, 1, 1000, nullptr);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    LOGI(L"主循环退出，开始清理资源");

    if (hLargeFont) DeleteObject(hLargeFont);
    if (hDateFont) DeleteObject(hDateFont);
    if (hNormalFont) DeleteObject(hNormalFont);
    if (hSourceFont) DeleteObject(hSourceFont);
    if (g_hEmbeddedFont) RemoveFontMemResourceEx(g_hEmbeddedFont);
    if (g_fontLoadedFromPath && !g_cmdFontPath.empty()) {
        RemoveFontResourceExW(g_cmdFontPath.c_str(), FR_PRIVATE, 0);
    }

    g_exitFlag = true;
    if (g_syncThread.joinable()) {
        CancelPendingOperations();
        g_syncThread.join();
    }

    CleanupNTPClient();
    CleanupFullscreenModule();
    D2DRenderer_Cleanup();

    LOGI(L"退出: 退出码=%d", (int)msg.wParam);
    return (int)msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        CreateButtons(hWnd);
        UpdateButtonLayout(hWnd);
        if (!g_hideButtons) {
            ResetInterActionTimer();
            ShowButtons(hWnd);
        }
        // 窗口最小尺寸在 WM_GETMINMAXINFO 中动态处理，不需要调用 SetWindowMinSize
        if (!g_noHotKeys) {
            RegisterHotKey(hWnd, 1, 0, VK_F11);
            RegisterHotKey(hWnd, 2, MOD_CONTROL | MOD_SHIFT, 'Q');
            RegisterHotKey(hWnd, 3, 0, VK_F5);
            RegisterHotKey(hWnd, 4, 0, VK_F6);
        }
        break;
    }

    case WM_ERASEBKGND: {
        // 所有绘制都在 WM_PAINT 双缓冲中完成，系统无需擦除背景。
        // 配合 nullptr 背景画刷，彻底消除背景闪烁。
        return TRUE;
    }

    // WM_ENTERSIZEMOVE / WM_EXITSIZEMOVE 已移除：
    // 字体更新在 WM_SIZE → UpdateButtonLayout → CreateDynamicFonts 中实时完成，
    // 无需在拖拽结束时单独触发。

    case WM_DPICHANGED: {
        UINT newDpi = LOWORD(wParam);
        RECT* newRect = (RECT*)lParam;
        SetWindowPos(hWnd, NULL,
            newRect->left, newRect->top,
            newRect->right - newRect->left,
            newRect->bottom - newRect->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        UpdateButtonLayout(hWnd);
        if (D2DRenderer_IsAvailable()) {
            RECT clientRect;
            GetClientRect(hWnd, &clientRect);
            D2DRenderer_OnResize(hWnd, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top);
        }
        return 0;
    }

    case WM_SIZING: {
        RECT* pRect = (RECT*)lParam;
        int minWidth = ScaleByDpi(BASE_MIN_WINDOW_WIDTH, hWnd);
        int minHeight = ScaleByDpi(BASE_MIN_WINDOW_HEIGHT, hWnd);
        int newWidth = pRect->right - pRect->left;
        int newHeight = pRect->bottom - pRect->top;

        if (newWidth < minWidth) {
            if (wParam == WMSZ_LEFT || wParam == WMSZ_BOTTOMLEFT || wParam == WMSZ_TOPLEFT)
                pRect->left = pRect->right - minWidth;
            else
                pRect->right = pRect->left + minWidth;
        }
        if (newHeight < minHeight) {
            if (wParam == WMSZ_TOP || wParam == WMSZ_TOPLEFT || wParam == WMSZ_TOPRIGHT)
                pRect->top = pRect->bottom - minHeight;
            else
                pRect->bottom = pRect->top + minHeight;
        }

        int ratioW = pRect->right - pRect->left;
        int ratioH = pRect->bottom - pRect->top;
        if (ratioH <= 0 || ratioW <= 0) return TRUE;

        float minRatio = 6.0f / 5.0f;
        float currentRatio = (float)ratioW / ratioH;
        if (currentRatio < minRatio) {
            int targetWidth = (int)((pRect->bottom - pRect->top) * minRatio);
            if (wParam == WMSZ_LEFT || wParam == WMSZ_BOTTOMLEFT || wParam == WMSZ_TOPLEFT)
                pRect->left = pRect->right - targetWidth;
            else if (wParam == WMSZ_RIGHT || wParam == WMSZ_BOTTOMRIGHT || wParam == WMSZ_TOPRIGHT)
                pRect->right = pRect->left + targetWidth;
        }
        return TRUE;
    }

    case WM_SIZE: {
        UpdateButtonLayout(hWnd);
        if (D2DRenderer_IsAvailable()) {
            RECT rc;
            GetClientRect(hWnd, &rc);
            D2DRenderer_OnResize(hWnd, rc.right - rc.left, rc.bottom - rc.top);
        }
        InvalidateRect(hWnd, NULL, FALSE);
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT clientRect;
        GetClientRect(hWnd, &clientRect);
        int width = clientRect.right - clientRect.left;
        int height = clientRect.bottom - clientRect.top;

        if (width <= 0 || height <= 0) { EndPaint(hWnd, &ps); break; }

        std::wstring sourceText = GetTimeSourceString();
        std::wstring timeDateStr = GetTimeDateString();
        size_t firstNewLine = timeDateStr.find(L'\n');
        size_t secondNewLine = timeDateStr.find(L'\n', firstNewLine + 1);
        std::wstring timeStr = timeDateStr.substr(0, firstNewLine);
        std::wstring dateStr = timeDateStr.substr(firstNewLine + 1, secondNewLine - firstNewLine - 1);
        std::wstring weekStr = timeDateStr.substr(secondNewLine + 1);

        bool d2dRendered = false;
        if (D2DRenderer_IsAvailable()) {
            FontSizes fz = CalculateFontSizes(hWnd);
            D2DRenderInfo info{};
            info.timeStr      = timeStr.c_str();
            info.dateStr      = dateStr.c_str();
            info.weekStr      = weekStr.c_str();
            info.sourceStr    = sourceText.c_str();
            info.fontFamily   = GetActiveFontFamily();
            info.timeFontSize   = fz.time;
            info.dateFontSize   = fz.date;
            info.weekFontSize   = fz.week;
            info.sourceFontSize = fz.source;
            info.boldTime       = true;
            d2dRendered = D2DRenderer_Render(hWnd, info);
        }
        if (!d2dRendered) {
            HDC memDC = CreateCompatibleDC(hdc);
            if (!memDC) { EndPaint(hWnd, &ps); break; }
            HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
            if (!memBitmap) { DeleteDC(memDC); EndPaint(hWnd, &ps); break; }
            HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

            HBRUSH hBrush = CreateSolidBrush(RGB(10, 10, 20));
            FillRect(memDC, &clientRect, hBrush);
            DeleteObject(hBrush);

            if (!sourceText.empty()) {
                // 动态计算来源文字所需高度
                HGDIOBJ hOldSourceFont = SelectObject(memDC, hSourceFont);
                TEXTMETRIC tm;
                GetTextMetrics(memDC, &tm);
                int sourceHeight = tm.tmHeight + 6; // 字体高度 + 少量边距
                RECT sourceRect = clientRect;
                sourceRect.left = 12;
                sourceRect.top = 8;
                sourceRect.right = clientRect.right - 12;
                sourceRect.bottom = sourceRect.top + sourceHeight;

                SetTextColor(memDC, RGB(140, 140, 160));
                SetBkMode(memDC, TRANSPARENT);
                DrawTextW(memDC, sourceText.c_str(), -1, &sourceRect,
                    DT_LEFT | DT_TOP | DT_NOPREFIX | DT_SINGLELINE);
                SelectObject(memDC, hOldSourceFont);
            }

            int windowHeight = clientRect.bottom - clientRect.top;
            int timeHeight = windowHeight / 3;
            int dateHeight = windowHeight / 8;
            int weekHeight = windowHeight / 8;
            int totalHeight = timeHeight + dateHeight + weekHeight + 20;
            int startY = (windowHeight - totalHeight) / 2 - 20;

            RECT timeRect = clientRect;
            timeRect.top = startY; timeRect.bottom = startY + timeHeight;
            HGDIOBJ hOldFont = SelectObject(memDC, hLargeFont);
            SetTextColor(memDC, RGB(220, 220, 255));
            SetBkMode(memDC, TRANSPARENT);
            DrawTextW(memDC, timeStr.c_str(), -1, &timeRect, DT_CENTER | DT_VCENTER | DT_NOPREFIX | DT_SINGLELINE);

            RECT dateRect = clientRect;
            dateRect.top = startY + timeHeight + 10; dateRect.bottom = dateRect.top + dateHeight;
            HGDIOBJ hOldDateFont = SelectObject(memDC, hDateFont);
            SetTextColor(memDC, RGB(200, 200, 240));
            DrawTextW(memDC, dateStr.c_str(), -1, &dateRect, DT_CENTER | DT_VCENTER | DT_NOPREFIX | DT_SINGLELINE);

            RECT weekRect = clientRect;
            weekRect.top = dateRect.bottom + 10; weekRect.bottom = weekRect.top + weekHeight;
            SetTextColor(memDC, RGB(180, 180, 220));
            DrawTextW(memDC, weekStr.c_str(), -1, &weekRect, DT_CENTER | DT_VCENTER | DT_NOPREFIX | DT_SINGLELINE);

            SelectObject(memDC, hOldDateFont);
            SelectObject(memDC, hOldFont);
            BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

            SelectObject(memDC, oldBitmap);
            DeleteObject(memBitmap);
            DeleteDC(memDC);
        }
        EndPaint(hWnd, &ps);
        break;
    }

    case WM_TIMER: {
        if (wParam == 1) {
            static std::wstring lastTimeStr, lastSourceStr;
            std::wstring currentStr = GetTimeDateString();
            std::wstring currentSource = GetTimeSourceString();
            bool needRedraw = false;
            if (currentStr != lastTimeStr) { lastTimeStr = currentStr; needRedraw = true; }
            if (currentSource != lastSourceStr) { lastSourceStr = currentSource; needRedraw = true; }
            if (needRedraw) InvalidateRect(hWnd, NULL, FALSE);
            CheckAutoHide(hWnd);
        }
        else if (wParam == 2) {
            if (hSyncBtn) SetWindowTextW(hSyncBtn, L"同步 (F5)");
            KillTimer(hWnd, 2);
        }
        else if (wParam == TIMER_AUTO_SYNC) {
            if (!g_syncing) StartSync(hWnd);
        }
        break;
    }

    case WM_COMMAND: {
        WORD ctrlId = LOWORD(wParam);
        if (ctrlId == ID_FULLSCREEN_BTN) {
            ToggleFullscreen(hWnd);
            if (hFullscreenBtn) SetWindowTextW(hFullscreenBtn, IsFullscreen() ? L"窗口化 (F11)" : L"全屏 (F11)");
        }
        else if (ctrlId == ID_EXIT_BTN) {
            PostMessage(hWnd, WM_CLOSE, 0, 0);
        }
        else if (ctrlId == ID_SYNC_BTN) {
            StartSync(hWnd);
            ShowButtons(hWnd);
            ResetInterActionTimer();
            g_manualShowActive = false;
        }
        else if (ctrlId == ID_TOPMOST_BTN) {
            ToggleTopmost(hWnd);
        }
        break;
    }

    case WM_KEYDOWN: {
        if (!g_hideButtons) {
            ShowButtons(hWnd);
            ResetInterActionTimer();
        }
        if (!g_noHotKeys) {
            if (wParam == VK_F11) {
                ToggleFullscreen(hWnd);
                if (hFullscreenBtn) SetWindowTextW(hFullscreenBtn, IsFullscreen() ? L"窗口化 (F11)" : L"全屏 (F11)");
            }
            else if (wParam == VK_ESCAPE) {
                PostMessage(hWnd, WM_CLOSE, 0, 0);
            }
            else if (wParam == VK_F5) {
                StartSync(hWnd);
            }
            else if (wParam == VK_F6) {
                ToggleTopmost(hWnd);
            }
        }
        break;
    }

    case WM_NCLBUTTONDBLCLK: {
        bool handled = false;
        if (!g_disableTouch) {
            handled = HandleNcLButtonDblClk(hWnd, wParam);
            if (handled && hFullscreenBtn) {
                SetWindowTextW(hFullscreenBtn, IsFullscreen() ? L"窗口化 (F11)" : L"全屏 (F11)");
            }
        }
        if (!handled) return DefWindowProcW(hWnd, message, wParam, lParam);
        break;
    }

    case WM_GETMINMAXINFO: {
        LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;
        int minWidth = ScaleByDpi(BASE_MIN_WINDOW_WIDTH, hWnd);
        int minHeight = ScaleByDpi(BASE_MIN_WINDOW_HEIGHT, hWnd);
        RECT rc = { 0, 0, minWidth, minHeight };
        AdjustWindowRectEx(&rc, GetWindowLong(hWnd, GWL_STYLE), FALSE,
            GetWindowLong(hWnd, GWL_EXSTYLE));
        lpMMI->ptMinTrackSize.x = rc.right - rc.left;
        lpMMI->ptMinTrackSize.y = rc.bottom - rc.top;
        break;
    }

    case WM_LBUTTONUP: {
        if (!g_hideButtons && hFullscreenBtn && hExitBtn && hSyncBtn && hTopmostBtn) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            bool inBtn = false;
            HWND btns[] = { hFullscreenBtn, hExitBtn, hSyncBtn, hTopmostBtn };
            for (HWND btn : btns) {
                if (IsWindowVisible(btn)) {
                    RECT btnRect;
                    GetWindowRect(btn, &btnRect);
                    MapWindowPoints(HWND_DESKTOP, hWnd, (LPPOINT)&btnRect, 2);
                    if (PtInRect(&btnRect, pt)) {
                        inBtn = true;
                        break;
                    }
                }
            }
            if (!inBtn) {
                if (g_buttonsVisible) {
                    if (g_manualShowActive) {
                        HideButtons(hWnd);
                        g_manualShowActive = false;
                        ResetInterActionTimer();
                    } else {
                        g_manualShowActive = true;
                        g_manualShowStartTime = GetTickCount64();
                    }
                } else {
                    ShowButtons(hWnd);
                    g_manualShowActive = true;
                    g_manualShowStartTime = GetTickCount64();
                }
            }
        }
        break;
    }

    case WM_MOUSEMOVE: {
        if (!g_hideButtons) {
            if (!g_buttonsVisible) {
                ShowButtons(hWnd);
            }
            ResetInterActionTimer();
            g_manualShowActive = false;
        }
        TRACKMOUSEEVENT tme = {};
        tme.cbSize = sizeof(TRACKMOUSEEVENT);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hWnd;
        TrackMouseEvent(&tme);
        break;
    }

    case WM_MOUSELEAVE: {
        if (!g_hideButtons) {
            ResetInterActionTimer();
        }
        break;
    }

    case WM_TOUCH: {
        if (g_disableTouch) {
            CloseTouchInputHandle((HTOUCHINPUT)lParam);
            return 0;
        }
        HandleTouchInput(hWnd, wParam, lParam);
        return 0;
    }

    case WM_HOTKEY: {
        if (!g_hideButtons) {
            ShowButtons(hWnd);
            ResetInterActionTimer();
        }
        if (wParam == 1) {
            ToggleFullscreen(hWnd);
            if (hFullscreenBtn) SetWindowTextW(hFullscreenBtn, IsFullscreen() ? L"窗口化 (F11)" : L"全屏 (F11)");
        }
        else if (wParam == 2) PostMessage(hWnd, WM_CLOSE, 0, 0);
        else if (wParam == 3) StartSync(hWnd);
        else if (wParam == 4) ToggleTopmost(hWnd);
        return 0;
    }

    case WM_NTP_SYNC_FINISHED: {
        bool syncOk = false;
        if (wParam == 1) {
            SYSTEMTIME ntpHour;
            if (GetLastNetworkTime(&ntpHour)) {
                g_networkTime = ntpHour;
                g_hasNetworkTime = true;
                g_syncBaseTick = GetTickCount64();
                syncOk = true;
            }
        }
        if (syncOk) {
            // 成功：继续显示“网络时间”，并去掉“同步中”
            if (hSyncBtn) SetWindowTextW(hSyncBtn, L"同步成功");
        }
        else {
            // 失败：时间来源回退为“本地时间”，并去掉“同步中”
            g_hasNetworkTime = false;
            g_syncBaseTick = 0;
            if (hSyncBtn) SetWindowTextW(hSyncBtn, L"同步失败");
        }
        g_syncing = false;
        // 立即刷新左上角来源文字（网络时间 / 本地时间）
        InvalidateRect(hWnd, NULL, FALSE);
        if (hSyncBtn) EnableWindow(hSyncBtn, TRUE);
        ResetInterActionTimer();
        SetTimer(hWnd, 2, 3000, NULL);
        break;
    }

    case WM_POWERBROADCAST: {
        if (wParam == PBT_APMRESUMEAUTOMATIC) {
            D2DRenderer_Cleanup();
            D2DRenderer_Init(hWnd);
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return TRUE;
    }

    case WM_DESTROY: {
        KillTimer(hWnd, 1);
        KillTimer(hWnd, TIMER_AUTO_SYNC);
        if (!g_noHotKeys) {
            UnregisterHotKey(hWnd, 1);
            UnregisterHotKey(hWnd, 2);
            UnregisterHotKey(hWnd, 3);
            UnregisterHotKey(hWnd, 4);
        }
        PostQuitMessage(0);
        break;
    }

    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}
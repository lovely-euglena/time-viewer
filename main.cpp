// main.cpp
// Windows 11风格时间显示主程序
// 修改：基于 DPI 动态调整所有尺寸上限（按钮、字体、窗口最小尺寸）

// 必须最先包含 winsock2.h，避免与 windows.h 冲突
#include <winsock2.h>
#include <ws2tcpip.h>

// 然后包含其他 Windows 头文件
#include <windows.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <shellapi.h>
// 其余头文件
#include <string>
#include <thread>
#include <atomic>
#include "fullscreen.h"
#include "ntp_client.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")

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
bool g_givehelp = false;

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

int GetDisplayTime(SYSTEMTIME& st);
std::wstring GetTimeDateStringEx();
std::wstring GetTimeSourceString();

// ==================== 函数实现 ====================

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
    if (g_hasNetworkTime) return L"🌐 网络时间";
    return L"💻 本地时间";
}

std::wstring GetTimeDateStringEx() {
    SYSTEMTIME st;
    GetDisplayTime(st);

    wchar_t timeStr[64];
    wsprintfW(timeStr, L"%02d:%02d:%02d\n%04d-%02d-%02d",
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

    int newBtnWidth = max(minBtnWidth, min(maxBtnWidth, clientWidth / 7));
    int newBtnHeight = max(minBtnHeight, min(maxBtnHeight, clientHeight / 12));

    int spacing = max(8, (int)(newBtnWidth * 0.2f));
    int marginBottom = max(15, (int)(clientHeight * 0.03f));

    int totalWidth = newBtnWidth * 4 + spacing * 3;
    int startX = max(0, (clientWidth - totalWidth) / 2);
    int btnY = clientHeight - newBtnHeight - marginBottom;

    g_buttonWidth = newBtnWidth;
    g_buttonHeight = newBtnHeight;

    SetWindowPos(hFullscreenBtn, NULL, startX, btnY, newBtnWidth, newBtnHeight, SWP_NOZORDER);
    SetWindowPos(hExitBtn, NULL, startX + newBtnWidth + spacing, btnY, newBtnWidth, newBtnHeight, SWP_NOZORDER);
    SetWindowPos(hSyncBtn, NULL, startX + (newBtnWidth + spacing) * 2, btnY, newBtnWidth, newBtnHeight, SWP_NOZORDER);
    SetWindowPos(hTopmostBtn, NULL, startX + (newBtnWidth + spacing) * 3, btnY, newBtnWidth, newBtnHeight, SWP_NOZORDER);

    CreateDynamicFonts(hWnd);
}

void CreateDynamicFonts(HWND hWnd) {
    FontSizes sizes = CalculateFontSizes(hWnd);

    if (hLargeFont) { DeleteObject(hLargeFont); hLargeFont = nullptr; }
    if (hDateFont) { DeleteObject(hDateFont); hDateFont = nullptr; }
    if (hNormalFont) { DeleteObject(hNormalFont); hNormalFont = nullptr; }
    if (hSourceFont) { DeleteObject(hSourceFont); hSourceFont = nullptr; }

    hLargeFont = CreateFontW(
        -sizes.time, 0, 0, 0, FW_BOLD,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"
    );

    hDateFont = CreateFontW(
        -sizes.date, 0, 0, 0, FW_SEMIBOLD,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"
    );

    hSourceFont = CreateFontW(
        -sizes.source, 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"
    );

    if (hFullscreenBtn && hExitBtn && hSyncBtn && hTopmostBtn && g_buttonHeight > 0) {
        int minBtnFont = ScaleByDpi(BASE_MIN_BTN_FONT, hWnd);
        int btnFontSize = max(minBtnFont, (int)(g_buttonHeight * 0.4));
        HFONT hBtnFont = CreateFontW(
            -btnFontSize, 0, 0, 0, FW_MEDIUM,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"
        );
        SendMessageW(hFullscreenBtn, WM_SETFONT, (WPARAM)hBtnFont, TRUE);
        SendMessageW(hExitBtn, WM_SETFONT, (WPARAM)hBtnFont, TRUE);
        SendMessageW(hSyncBtn, WM_SETFONT, (WPARAM)hBtnFont, TRUE);
        SendMessageW(hTopmostBtn, WM_SETFONT, (WPARAM)hBtnFont, TRUE);
        hNormalFont = hBtnFont;
    }
}

void HandleTouchInput(HWND hWnd, WPARAM wParam, LPARAM lParam) {
    UINT cInputs = LOWORD(wParam);
    PTOUCHINPUT pInputs = new TOUCHINPUT[cInputs];
    if (pInputs) {
        if (GetTouchInputInfo((HTOUCHINPUT)lParam, cInputs, pInputs, sizeof(TOUCHINPUT))) {
            POINT ptInput;
            for (UINT i = 0; i < cInputs; i++) {
                TOUCHINPUT ti = pInputs[i];
                ptInput.x = TOUCH_COORD_TO_PIXEL(ti.x);
                ptInput.y = TOUCH_COORD_TO_PIXEL(ti.y);
                ScreenToClient(hWnd, &ptInput);
                if (ti.dwFlags & TOUCHEVENTF_DOWN) {
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
}

void StartSync(HWND hWnd) {
    if (g_syncing) return;
    g_syncing = true;
    if (hSyncBtn) SetWindowTextW(hSyncBtn, L"同步中...");
    if (g_syncThread.joinable()) g_syncThread.join();
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
    sizes.time = max(minTimeFont, min(sizes.time, (int)(windowHeight * 0.45)));

    sizes.date = (int)(sizes.time * 0.3);
    int minDateFont = ScaleByDpi(BASE_MIN_DATE_FONT, hWnd);
    sizes.date = max(minDateFont, min(sizes.date, (int)(windowHeight * 0.12)));

    sizes.week = (int)(sizes.time * 0.3);
    int minWeekFont = ScaleByDpi(BASE_MIN_WEEK_FONT, hWnd);
    sizes.week = max(minWeekFont, min(sizes.week, (int)(windowHeight * 0.1)));

    // 来源字体：基准范围 10~26，随 DPI 缩放
    int baseSourceMin = 10;
    int baseSourceMax = 16;
    int minSourceFont = ScaleByDpi(baseSourceMin, hWnd);
    int maxSourceFont = ScaleByDpi(baseSourceMax, hWnd);
    int rawSource = (int)(sizes.time * 0.25);
    sizes.source = max(minSourceFont, min(maxSourceFont, rawSource));

    return sizes;
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

/*
void help(int page = 0) {
    using std::cout;
    using std::string;
    using std::endl;

    const string help_header =
        "╔══════════════════════════════════════════════════════════════════╗\n"
        "║                    TimeViewer 时间显示器 v1.0                     ║\n"
        "║               Windows 11 风格网络时间同步工具                      ║\n"
        "╚══════════════════════════════════════════════════════════════════╝\n";

    const string help_usage =
        "\n使用方法:\n"
        "  TimeViewer.exe [选项]\n";

    const string help_options_page0 =
        "\n【命令行选项】\n"
        "  -b, --no-button       隐藏所有按钮（全屏、退出、同步、置顶）\n"
        "  -f, --full-screen     启动时自动进入全屏模式\n"
        "  -k, --nok-key         禁用全局热键（F11, F5, F6, Ctrl+Shift+Q）\n"
        "  -p, --topmost         启动时自动置顶窗口\n"
        "  -t, --transparency    设置窗口透明度 (0-255, 0=完全透明, 255=不透明)\n"
        "  -h, --help            显示此帮助信息\n"
        "  -v, --version         显示版本信息\n";

    const string help_options_page1 =
        "\n【热键说明】（默认启用，可用 -k 禁用）\n"
        "  F11                   切换全屏/窗口模式\n"
        "  F5                    手动同步网络时间（NTP）\n"
        "  F6                    切换窗口置顶状态\n"
        "  Ctrl+Shift+Q          退出程序\n"
        "  双击标题栏（或触摸双击） 切换全屏模式\n";

    const string help_options_page2 =
        "\n【功能特性】\n"
        "  • NTP网络时间同步（自动/手动）\n"
        "  • 多显示器DPI自适应\n"
        "  • 窗口圆角效果（Windows 11风格）\n"
        "  • 透明度调节\n"
        "  • 触摸屏支持（双击切换全屏）\n"
        "  • 自动同步间隔：1小时\n"
        "  • 最小窗口尺寸：基于DPI动态调整（基准600×400）\n"
        "  • 长宽比限制：最小6:5\n";

    const string help_tips =
        "\n【使用示例】\n"
        "  TimeViewer.exe                          # 普通模式启动\n"
        "  TimeViewer.exe -f -p                    # 全屏+置顶启动\n"
        "  TimeViewer.exe --no-button --transparency 200  # 无按钮半透明\n"
        "  TimeViewer.exe -k -b                    # 禁用热键并隐藏按钮\n";

    const string help_footer =
        "\n【项目信息】\n"
        "  开源地址：https://gitee.com/ygwill/time-viewer\n"
        "  问题反馈：请在Gitee上提交Issue\n"
        "\n";

    switch (page) {
    case 0:
        cout << help_header << help_usage << help_options_page0;
        break;
    case 1:
        cout << help_options_page1;
        break;
    case 2:
        cout << help_options_page2;
        break;
    case 3:
        cout << help_tips;
        break;
    case 4:
        cout << help_footer;
        break;
    default:
        cout << help_header << help_usage << help_options_page0
            << help_options_page1 << help_options_page2
            << help_tips << help_footer;
        break;
    }

    if (page >= 0 && page <= 4 && page != -1) {
        cout << "\n提示：使用 'TimeViewer.exe -h' 查看完整帮助信息\n";
    }
}
*/

int WINAPI wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow) {

    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (wcscmp(argv[i], L"--no-button") == 0 || wcscmp(argv[i], L"-b") == 0)
                g_hideButtons = true;
            else if (wcscmp(argv[i], L"--full-screen") == 0 || wcscmp(argv[i], L"-f") == 0)
                g_startFullscreen = true;
            else if (wcscmp(argv[i], L"--nok-key") == 0 || wcscmp(argv[i], L"-k") == 0)
                g_noHotKeys = true;
            else if (wcscmp(argv[i], L"--topmost") == 0 || wcscmp(argv[i], L"-p") == 0)
                g_startTopmost = true;
            else if (wcscmp(argv[i], L"--help") == 0 || wcscmp(argv[i], L"-h") == 0)
                g_givehelp = true;
            else if ((wcscmp(argv[i], L"--transparency") == 0 || wcscmp(argv[i], L"-t") == 0) && i + 1 < argc) {
                int val = _wtoi(argv[++i]);
                if (val >= 0 && val <= 255) g_transparency = val;
            }
        }
        LocalFree(argv);
    }

    HMODULE hUser32 = LoadLibraryW(L"user32.dll");
    if (hUser32) {
        auto pSetProcessDpiAwarenessContext = (BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT))GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pSetProcessDpiAwarenessContext) {
            pSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
        FreeLibrary(hUser32);
    }
    else {
        SetProcessDPIAware();
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
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = CreateSolidBrush(RGB(10, 10, 20));
    wcex.lpszClassName = CLASS_NAME;
    wcex.hIcon = LoadIconW(hInstance, MAKEINTRESOURCE(101));
    wcex.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCE(101));

    if (!RegisterClassExW(&wcex)) {
        DWORD err = GetLastError();
        wchar_t msg[256];
        wsprintfW(msg, L"窗口类注册失败！\n错误码: %lu\ncbSize: %u", err, wcex.cbSize);
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

    hMainWindow = CreateWindowExW(
        WS_EX_APPWINDOW | (g_transparency < 255 ? WS_EX_LAYERED : 0),
        CLASS_NAME,
        L"时间显示器",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        initWidth, initHeight,
        nullptr, nullptr, hInstance, nullptr);

    if (!hMainWindow) {
        MessageBoxW(nullptr, L"主窗口创建失败！", L"错误", MB_ICONERROR);
        return 1;
    }

    if (g_transparency < 255) {
        SetLayeredWindowAttributes(hMainWindow, 0, (BYTE)g_transparency, LWA_ALPHA);
    }

    DWM_WINDOW_CORNER_PREFERENCE cornerPreference = DWMWCP_ROUND;
    DwmSetWindowAttribute(hMainWindow, DWMWA_WINDOW_CORNER_PREFERENCE,
        &cornerPreference, sizeof(cornerPreference));

    RegisterTouchWindow(hMainWindow, TWF_FINETOUCH);

    if (!InitializeNTPClient()) {
        OutputDebugStringW(L"NTP客户端初始化失败\n");
    }

    CreateDynamicFonts(hMainWindow);
    InitializeFullscreenModule(hMainWindow);

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

    if (hLargeFont) DeleteObject(hLargeFont);
    if (hDateFont) DeleteObject(hDateFont);
    if (hNormalFont) DeleteObject(hNormalFont);
    if (hSourceFont) DeleteObject(hSourceFont);

    g_exitFlag = true;
    if (g_syncThread.joinable()) g_syncThread.join();

    CleanupNTPClient();
    CleanupFullscreenModule();

    return (int)msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        CreateButtons(hWnd);
        UpdateButtonLayout(hWnd);
        // 窗口最小尺寸在 WM_GETMINMAXINFO 中动态处理，不需要调用 SetWindowMinSize
        if (!g_noHotKeys) {
            RegisterHotKey(hWnd, 1, 0, VK_F11);
            RegisterHotKey(hWnd, 2, MOD_CONTROL | MOD_SHIFT, 'Q');
            RegisterHotKey(hWnd, 3, 0, VK_F5);
            RegisterHotKey(hWnd, 4, 0, VK_F6);
        }
        break;
    }

    case WM_DPICHANGED: {
        UINT newDpi = LOWORD(wParam);
        RECT* newRect = (RECT*)lParam;
        SetWindowPos(hWnd, NULL,
            newRect->left, newRect->top,
            newRect->right - newRect->left,
            newRect->bottom - newRect->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        UpdateButtonLayout(hWnd);
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

        float minRatio = 6.0f / 5.0f;
        float currentRatio = (float)(pRect->right - pRect->left) / (pRect->bottom - pRect->top);
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

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

        HBRUSH hBrush = CreateSolidBrush(RGB(10, 10, 20));
        FillRect(memDC, &clientRect, hBrush);
        DeleteObject(hBrush);

        std::wstring sourceText = GetTimeSourceString();
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

        std::wstring timeDateStr = GetTimeDateString();
        size_t firstNewLine = timeDateStr.find(L'\n');
        size_t secondNewLine = timeDateStr.find(L'\n', firstNewLine + 1);
        std::wstring timeStr = timeDateStr.substr(0, firstNewLine);
        std::wstring dateStr = timeDateStr.substr(firstNewLine + 1, secondNewLine - firstNewLine - 1);
        std::wstring weekStr = timeDateStr.substr(secondNewLine + 1);

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
        }
        else if (ctrlId == ID_TOPMOST_BTN) {
            ToggleTopmost(hWnd);
        }
        break;
    }

    case WM_KEYDOWN: {
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
        if (HandleNcLButtonDblClk(hWnd, wParam)) {
            if (hFullscreenBtn) SetWindowTextW(hFullscreenBtn, IsFullscreen() ? L"窗口化 (F11)" : L"全屏 (F11)");
        }
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
        HandleGetMinMaxInfo(lParam);
        break;
    }

    case WM_TOUCH: {
        HandleTouchInput(hWnd, wParam, lParam);
        return 0;
    }

    case WM_HOTKEY: {
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
        if (wParam == 1) {
            SYSTEMTIME ntpHour;
            if (GetLastNetworkTime(&ntpHour)) {
                g_networkTime = ntpHour;
                g_hasNetworkTime = true;
                if (hSyncBtn) SetWindowTextW(hSyncBtn, L"同步成功");
                InvalidateRect(hWnd, NULL, FALSE);
                g_syncBaseTick = GetTickCount64();
            }
            else {
                if (hSyncBtn) SetWindowTextW(hSyncBtn, L"同步失败");
            }
        }
        else {
            if (hSyncBtn) SetWindowTextW(hSyncBtn, L"同步失败");
        }
        if (hSyncBtn) EnableWindow(hSyncBtn, TRUE);
        g_syncing = false;
        SetTimer(hWnd, 2, 3000, NULL);
        break;
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
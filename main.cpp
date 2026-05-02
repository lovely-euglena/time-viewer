// main.cpp
// Windows 11风格时间显示主程序

// 必须最先包含 winsock2.h，避免与 windows.h 冲突
#include <winsock2.h>
#include <ws2tcpip.h>

// 然后包含其他 Windows 头文件
#include <windows.h>
#include <dwmapi.h>
#include <commctrl.h>

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

// 自定义消息：NTP同步完成
#define WM_NTP_SYNC_FINISHED (WM_USER + 100)

// 自动同步定时器ID
#define TIMER_AUTO_SYNC     3
#define AUTO_SYNC_INTERVAL_MS  3600000  // 1小时 = 3600000毫秒

// 窗口过程函数声明
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

// 全局变量
HFONT hLargeFont = nullptr;     // 大字体用于时间显示
HFONT hDateFont = nullptr;      // 日期字体
HFONT hNormalFont = nullptr;    // 按钮字体
HFONT hSourceFont = nullptr;    // 时间来源指示器字体（小字体）
HWND hMainWindow = nullptr;     // 主窗口句柄
HWND hFullscreenBtn = nullptr;  // 全屏按钮句柄
HWND hExitBtn = nullptr;        // 退出按钮句柄
HWND hSyncBtn = nullptr;        // NTP同步按钮句柄

std::atomic<bool> g_syncing{ false };   // 同步进行中标志
std::atomic<bool> g_exitFlag{ false };  // 程序退出标志
std::thread g_syncThread;               // 同步线程对象

// ===== 命令行参数标志 =====
bool g_hideButtons = false;      // 是否隐藏按钮
bool g_startFullscreen = false;  // 是否启动即全屏
bool g_noHotKeys = false;        // 是否不注册热键

// ===== 网络时间存储（永不过期，成功获取后一直使用）=====
static SYSTEMTIME g_networkTime = { 0 };     // 最后一次成功同步的网络时间
static bool g_hasNetworkTime = false;      // 是否已获取过有效的网络时间
static ULONGLONG g_syncBaseTick = 0;  // 同步时刻的本地 TickCount (毫秒)

struct FontSizes {
    int time;
    int date;
    int week;
    int button;
    int source;
};

// ==================== 函数声明 ====================
std::wstring GetTimeDateString();
void CreateControls(HWND hWnd);
void HandleTouchInput(HWND hWnd, WPARAM wParam, LPARAM lParam);
void CreateDynamicFonts(HWND hWnd);
void StartSync(HWND hWnd);               // 启动异步同步
void SyncThreadProc(HWND hWnd);          // 同步线程函数
FontSizes CalculateFontSizes(HWND hWnd);

// ===== 时间源管理函数 =====
int GetDisplayTime(SYSTEMTIME& st);
std::wstring GetTimeDateStringEx();
std::wstring GetTimeSourceString();       // 获取时间来源文字

// ==================== 函数实现 ====================

// 1. 获取当前显示时间（网络时间优先，永不过期）
int GetDisplayTime(SYSTEMTIME& st) {
    if (g_hasNetworkTime) {
        // 计算流逝的毫秒数
        ULONGLONG elapsedMs = GetTickCount64() - g_syncBaseTick;
        // 将网络时间转为 FILETIME（100纳秒单位）
        FILETIME ft;
        SystemTimeToFileTime(&g_networkTime, &ft);
        ULARGE_INTEGER uli;
        uli.LowPart = ft.dwLowDateTime;
        uli.HighPart = ft.dwHighDateTime;
        // 加上流逝的时间（毫秒 → 100纳秒）
        uli.QuadPart += elapsedMs * 10000ULL;
        // 转回 SYSTEMTIME
        ft.dwLowDateTime = uli.LowPart;
        ft.dwHighDateTime = uli.HighPart;
        FileTimeToSystemTime(&ft, &st);
        return 1;
    }
    GetLocalTime(&st);
    return 0;
}

// 2. 获取时间来源文字（用于左上角显示）
std::wstring GetTimeSourceString() {
    if (g_hasNetworkTime) {
        return L"🌐 网络时间";
    }
    return L"💻 本地时间";
}

// 3. 生成显示字符串
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

// 兼容原有调用
std::wstring GetTimeDateString() {
    return GetTimeDateStringEx();
}

// 4. 动态字体创建函数
void CreateDynamicFonts(HWND hWnd) {
    FontSizes sizes = CalculateFontSizes(hWnd);

    if (hLargeFont) {
        DeleteObject(hLargeFont);
        hLargeFont = nullptr;
    }
    if (hDateFont) {
        DeleteObject(hDateFont);
        hDateFont = nullptr;
    }
    if (hNormalFont) {
        DeleteObject(hNormalFont);
        hNormalFont = nullptr;
    }
    if (hSourceFont) {
        DeleteObject(hSourceFont);
        hSourceFont = nullptr;
    }

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

    hNormalFont = CreateFontW(
        -sizes.week, 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"
    );

    // 创建时间来源指示器的小字体
    hSourceFont = CreateFontW(
        -sizes.source, 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"
    );

    // 如果按钮存在，单独创建按钮字体并发送
    if (hFullscreenBtn && hExitBtn && hSyncBtn) {
        HFONT hBtnFont = CreateFontW(
            -sizes.button, 0, 0, 0, FW_MEDIUM,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"
        );
        SendMessageW(hFullscreenBtn, WM_SETFONT, (WPARAM)hBtnFont, TRUE);
        SendMessageW(hExitBtn, WM_SETFONT, (WPARAM)hBtnFont, TRUE);
        SendMessageW(hSyncBtn, WM_SETFONT, (WPARAM)hBtnFont, TRUE);
        if (hNormalFont) DeleteObject(hNormalFont);
        hNormalFont = hBtnFont;
    }
}

// 5. 创建控件（依据 g_hideButtons 决定是否创建）
void CreateControls(HWND hWnd) {
    if (g_hideButtons) return;   // 命令行要求不显示按钮

    RECT clientRect;
    GetClientRect(hWnd, &clientRect);

    UINT dpi = GetWindowDpi(hWnd);
    float scaleFactor = dpi / 96.0f;

    int btnWidth = (int)(130 * scaleFactor);
    int btnHeight = (int)(40 * scaleFactor);
    int spacing = (int)(20 * scaleFactor);
    int btnY = clientRect.bottom - btnHeight - (int)(20 * scaleFactor);
    int totalWidth = btnWidth * 3 + spacing * 2;
    int startX = (clientRect.right - totalWidth) / 2;

    hFullscreenBtn = CreateWindowW(
        L"BUTTON", L"全屏 (F11)",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        startX, btnY, btnWidth, btnHeight,
        hWnd, (HMENU)ID_FULLSCREEN_BTN,
        (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE), NULL
    );

    hExitBtn = CreateWindowW(
        L"BUTTON", L"退出 (Esc)",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        startX + btnWidth + spacing, btnY, btnWidth, btnHeight,
        hWnd, (HMENU)ID_EXIT_BTN,
        (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE), NULL
    );

    hSyncBtn = CreateWindowW(
        L"BUTTON", L"同步时间 (F5)",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        startX + (btnWidth + spacing) * 2, btnY, btnWidth, btnHeight,
        hWnd, (HMENU)ID_SYNC_BTN,
        (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE), NULL
    );

    if (hNormalFont && hFullscreenBtn && hExitBtn && hSyncBtn) {
        SendMessageW(hFullscreenBtn, WM_SETFONT, (WPARAM)hNormalFont, TRUE);
        SendMessageW(hExitBtn, WM_SETFONT, (WPARAM)hNormalFont, TRUE);
        SendMessageW(hSyncBtn, WM_SETFONT, (WPARAM)hNormalFont, TRUE);
    }
}

// 6. 触摸输入处理
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

// 7. 启动异步同步
void StartSync(HWND hWnd) {
    if (g_syncing) return;
    g_syncing = true;
    if (hSyncBtn)
        SetWindowTextW(hSyncBtn, L"同步中...");
    if (g_syncThread.joinable()) {
        g_syncThread.join();
    }
    g_syncThread = std::thread(SyncThreadProc, hWnd);
}

// 8. 同步线程函数
void SyncThreadProc(HWND hWnd) {
    bool success = SyncTimeOnce();
    if (!g_exitFlag) {
        PostMessageW(hWnd, WM_NTP_SYNC_FINISHED, success ? 1 : 0, 0);
    }
}

// 9. 计算字体大小
FontSizes CalculateFontSizes(HWND hWnd) {
    RECT clientRect;
    GetClientRect(hWnd, &clientRect);
    UINT dpi = GetWindowDpi(hWnd);
    float scaleFactor = dpi / 96.0f;
    int windowHeight = clientRect.bottom - clientRect.top;

    FontSizes sizes;
    int targetTimeHeight = (int)(windowHeight * 0.35);
    sizes.time = (int)(targetTimeHeight * 0.75);
    sizes.time = max(40, min(sizes.time, (int)(windowHeight * 0.45)));
    sizes.date = (int)(sizes.time * 0.3);
    sizes.date = max(15, min(sizes.date, (int)(windowHeight * 0.12)));
    sizes.week = (int)(sizes.time * 0.3);
    sizes.week = max(15, min(sizes.week, (int)(windowHeight * 0.1)));
    sizes.button = max(15, min(25, windowHeight / 25));

    // 时间来源字体：主时间字体的 1/5，最小10px，最大18px
    sizes.source = max(10, min(18, (int)(sizes.time * 0.2)));

    return sizes;
}

// 10. 应用程序入口点
int WINAPI wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow) {

    // ---- 命令行参数解析 ----
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
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
        }
        LocalFree(argv);
    }

    // 设置DPI感知
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

    hMainWindow = CreateWindowExW(
        WS_EX_APPWINDOW,
        CLASS_NAME,
        L"时间显示器",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        nullptr, nullptr, hInstance, nullptr);

    if (!hMainWindow) {
        MessageBoxW(nullptr, L"主窗口创建失败！", L"错误", MB_ICONERROR);
        return 1;
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

    ShowWindow(hMainWindow, nCmdShow);
    UpdateWindow(hMainWindow);

    // 如果命令行要求启动即全屏
    if (g_startFullscreen) {
        ToggleFullscreen(hMainWindow);
        if (hFullscreenBtn)
            SetWindowTextW(hFullscreenBtn, IsFullscreen() ? L"窗口化 (F11)" : L"全屏 (F11)");
    }

    // 启动后立即手动同步一次（异步）
    StartSync(hMainWindow);

    // 设置自动同步定时器（1小时触发一次，由UI线程处理，保证线程安全）
    SetTimer(hMainWindow, TIMER_AUTO_SYNC, AUTO_SYNC_INTERVAL_MS, nullptr);

    // 设置时间刷新定时器（每秒）
    SetTimer(hMainWindow, 1, 1000, nullptr);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // 清理资源
    if (hLargeFont) DeleteObject(hLargeFont);
    if (hDateFont) DeleteObject(hDateFont);
    if (hNormalFont) DeleteObject(hNormalFont);
    if (hSourceFont) DeleteObject(hSourceFont);

    g_exitFlag = true;
    if (g_syncThread.joinable()) {
        g_syncThread.join();
    }

    CleanupNTPClient();
    CleanupFullscreenModule();

    return (int)msg.wParam;
}

// 11. 窗口过程函数
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        CreateControls(hWnd);
        SetWindowMinSize(hWnd, 550, 300);

        // 注册全局热键（除非被命令行禁用）
        if (!g_noHotKeys) {
            if (!RegisterHotKey(hWnd, 1, 0, VK_F11)) {
                OutputDebugStringW(L"全局热键 F11 注册失败\n");
            }
            if (!RegisterHotKey(hWnd, 2, MOD_CONTROL | MOD_SHIFT, 'Q')) {
                OutputDebugStringW(L"全局热键 Ctrl+Shift+Q 注册失败\n");
            }
            if (!RegisterHotKey(hWnd, 3, 0, VK_F5)) {
                OutputDebugStringW(L"全局热键 F5 注册失败\n");
            }
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
        CreateDynamicFonts(hWnd);
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_SIZING: {
        RECT* pRect = (RECT*)lParam;
        int newWidth = pRect->right - pRect->left;
        int newHeight = pRect->bottom - pRect->top;
        int minWidth = 550;
        int minHeight = 300;

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
        CreateDynamicFonts(hWnd);
        InvalidateRect(hWnd, NULL, FALSE);

        // 如果按钮存在，重新布局
        if (hFullscreenBtn && hExitBtn && hSyncBtn) {
            RECT clientRect;
            GetClientRect(hWnd, &clientRect);
            UINT dpi = GetWindowDpi(hWnd);
            float scaleFactor = dpi / 96.0f;
            int btnWidth = (int)(120 * scaleFactor);
            int btnHeight = (int)(40 * scaleFactor);
            int btnY = clientRect.bottom - btnHeight - (int)(20 * scaleFactor);
            int spacing = (int)(20 * scaleFactor);
            int totalWidth = btnWidth * 3 + spacing * 2;
            int startX = max(0, (clientRect.right - totalWidth) / 2);

            SetWindowPos(hFullscreenBtn, NULL, startX, btnY, btnWidth, btnHeight, SWP_NOZORDER);
            SetWindowPos(hExitBtn, NULL, startX + btnWidth + spacing, btnY, btnWidth, btnHeight, SWP_NOZORDER);
            SetWindowPos(hSyncBtn, NULL, startX + (btnWidth + spacing) * 2, btnY, btnWidth, btnHeight, SWP_NOZORDER);
        }
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT clientRect;
        GetClientRect(hWnd, &clientRect);
        int width = clientRect.right - clientRect.left;
        int height = clientRect.bottom - clientRect.top;

        if (width <= 0 || height <= 0) {
            EndPaint(hWnd, &ps);
            break;
        }

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

        HBRUSH hBrush = CreateSolidBrush(RGB(10, 10, 20));
        FillRect(memDC, &clientRect, hBrush);
        DeleteObject(hBrush);

        // 绘制时间来源指示器（左上角）
        std::wstring sourceText = GetTimeSourceString();
        if (!sourceText.empty()) {
            RECT sourceRect = clientRect;
            sourceRect.left = 12;
            sourceRect.top = 8;
            sourceRect.right = clientRect.right - 12;
            sourceRect.bottom = sourceRect.top + 30;

            HGDIOBJ hOldSourceFont = SelectObject(memDC, hSourceFont);
            SetTextColor(memDC, RGB(140, 140, 160));
            SetBkMode(memDC, TRANSPARENT);
            DrawTextW(memDC, sourceText.c_str(), -1, &sourceRect,
                DT_LEFT | DT_TOP | DT_NOPREFIX | DT_SINGLELINE);
            SelectObject(memDC, hOldSourceFont);
        }

        // 获取时间字符串
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
        timeRect.top = startY;
        timeRect.bottom = startY + timeHeight;
        HGDIOBJ hOldFont = SelectObject(memDC, hLargeFont);
        SetTextColor(memDC, RGB(220, 220, 255));
        SetBkMode(memDC, TRANSPARENT);
        DrawTextW(memDC, timeStr.c_str(), -1, &timeRect, DT_CENTER | DT_VCENTER | DT_NOPREFIX | DT_SINGLELINE);

        RECT dateRect = clientRect;
        dateRect.top = startY + timeHeight + 10;
        dateRect.bottom = dateRect.top + dateHeight;
        HGDIOBJ hOldDateFont = SelectObject(memDC, hDateFont);
        SetTextColor(memDC, RGB(200, 200, 240));
        DrawTextW(memDC, dateStr.c_str(), -1, &dateRect, DT_CENTER | DT_VCENTER | DT_NOPREFIX | DT_SINGLELINE);

        RECT weekRect = clientRect;
        weekRect.top = dateRect.bottom + 10;
        weekRect.bottom = weekRect.top + weekHeight;
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
            // 每秒刷新时间显示
            static std::wstring lastTimeStr;
            static std::wstring lastSourceStr;
            std::wstring currentStr = GetTimeDateString();
            std::wstring currentSource = GetTimeSourceString();

            bool needRedraw = false;
            if (currentStr != lastTimeStr) {
                lastTimeStr = currentStr;
                needRedraw = true;
            }
            if (currentSource != lastSourceStr) {
                lastSourceStr = currentSource;
                needRedraw = true;
            }
            if (needRedraw) {
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }
        else if (wParam == 2) {
            // 恢复按钮文字（3秒后）
            if (hSyncBtn)
                SetWindowTextW(hSyncBtn, L"同步时间 (F5)");
            KillTimer(hWnd, 2);
        }
        else if (wParam == TIMER_AUTO_SYNC) {
            // 每小时自动同步一次
            if (!g_syncing) {
                StartSync(hWnd);
            }
        }
        break;
    }

    case WM_COMMAND: {
        WORD ctrlId = LOWORD(wParam);
        if (ctrlId == ID_FULLSCREEN_BTN) {
            ToggleFullscreen(hWnd);
            if (hFullscreenBtn)
                SetWindowTextW(hFullscreenBtn, IsFullscreen() ? L"窗口化 (F11)" : L"全屏 (F11)");
        }
        else if (ctrlId == ID_EXIT_BTN) {
            PostMessage(hWnd, WM_CLOSE, 0, 0);
        }
        else if (ctrlId == ID_SYNC_BTN) {
            StartSync(hWnd);
        }
        break;
    }

    case WM_KEYDOWN: {
        if (!g_noHotKeys) {
            if (wParam == VK_F11) {
                ToggleFullscreen(hWnd);
                if (hFullscreenBtn)
                    SetWindowTextW(hFullscreenBtn, IsFullscreen() ? L"窗口化 (F11)" : L"全屏 (F11)");
            }
            else if (wParam == VK_ESCAPE) {
                PostMessage(hWnd, WM_CLOSE, 0, 0);
            }
            else if (wParam == VK_F5) {
                StartSync(hWnd);
            }
        }
        break;
    }

    case WM_NCLBUTTONDBLCLK: {
        if (HandleNcLButtonDblClk(hWnd, wParam)) {
            if (hFullscreenBtn)
                SetWindowTextW(hFullscreenBtn, IsFullscreen() ? L"窗口化 (F11)" : L"全屏 (F11)");
        }
        break;
    }

    case WM_GETMINMAXINFO: {
        LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;
        RECT rc = { 0, 0, 550, 300 };
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
            if (hFullscreenBtn)
                SetWindowTextW(hFullscreenBtn, IsFullscreen() ? L"窗口化 (F11)" : L"全屏 (F11)");
        }
        else if (wParam == 2) {
            PostMessage(hWnd, WM_CLOSE, 0, 0);
        }
        else if (wParam == 3) {
            StartSync(hWnd);
        }
        return 0;
    }

    case WM_NTP_SYNC_FINISHED: {
        if (wParam == 1) {
            SYSTEMTIME ntpHour;
            if (GetLastNetworkTime(&ntpHour)) {
                g_networkTime = ntpHour;
                g_hasNetworkTime = true;
                if (hSyncBtn)
                    SetWindowTextW(hSyncBtn, L"同步成功");
                InvalidateRect(hWnd, NULL, FALSE);
                g_syncBaseTick = GetTickCount64();
            }
            else {
                if (hSyncBtn)
                    SetWindowTextW(hSyncBtn, L"同步失败");
            }
        }
        else {
            if (hSyncBtn || hSyncBtn == 0)  SetWindowTextW(hSyncBtn, L"同步失败");
        }
        if (hSyncBtn || hSyncBtn == 0)   EnableWindow(hSyncBtn, TRUE);
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
        }
        PostQuitMessage(0);
        break;
    }

    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}
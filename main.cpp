// main.cpp
// Windows 11风格时间显示主程序
// 修改：扩大最小尺寸，按钮字体基于按钮高度

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

// 窗口过程函数声明
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

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

// ===== 新增：按钮实际高度（用于字体计算） =====
int g_buttonHeight = 40;        // 默认值，会在创建按钮时更新

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
void CreateControls(HWND hWnd);
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

    // 按钮字体不再通过 hNormalFont 混合使用，直接创建专用字体
    hSourceFont = CreateFontW(
        -sizes.source, 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"
    );

    // 按钮字体：基于 g_buttonHeight 计算，不受 sizes.button 影响
    if (hFullscreenBtn && hExitBtn && hSyncBtn && hTopmostBtn) {
        int btnFontSize = max(12, (int)(g_buttonHeight * 0.4));  // 按钮高度的40%，最小12px
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
        // 释放旧的 hNormalFont 并更新为新字体
        if (hNormalFont) DeleteObject(hNormalFont);
        hNormalFont = hBtnFont;
    }
}

void CreateControls(HWND hWnd) {
    if (g_hideButtons) return;

    RECT clientRect;
    GetClientRect(hWnd, &clientRect);
    UINT dpi = GetWindowDpi(hWnd);
    float scaleFactor = dpi / 96.0f;

    int btnWidth = (int)(105 * scaleFactor);
    int btnHeight = (int)(40 * scaleFactor);   // 按钮高度
    g_buttonHeight = btnHeight;                // 保存实际高度

    int spacing = (int)(15 * scaleFactor);
    int btnY = clientRect.bottom - btnHeight - (int)(20 * scaleFactor);
    int totalWidth = btnWidth * 4 + spacing * 3;
    int startX = (clientRect.right - totalWidth) / 2;

    hFullscreenBtn = CreateWindowW(
        L"BUTTON", g_noHotKeys ? L"全屏" : L"全屏 (F11)",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        startX, btnY, btnWidth, btnHeight,
        hWnd, (HMENU)ID_FULLSCREEN_BTN,
        (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE), NULL
    );

    hExitBtn = CreateWindowW(
        L"BUTTON", g_noHotKeys ? L"退出" : L"退出 (Esc)",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        startX + btnWidth + spacing, btnY, btnWidth, btnHeight,
        hWnd, (HMENU)ID_EXIT_BTN,
        (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE), NULL
    );

    hSyncBtn = CreateWindowW(
        L"BUTTON", g_noHotKeys ? L"同步时间" : L"同步 (F5)",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        startX + (btnWidth + spacing) * 2, btnY, btnWidth, btnHeight,
        hWnd, (HMENU)ID_SYNC_BTN,
        (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE), NULL
    );

    hTopmostBtn = CreateWindowW(
        L"BUTTON", g_topmost ? (g_noHotKeys ? L"取消置顶" : L"消顶 (F6)") : (g_noHotKeys ? L"置顶" : L"置顶 (F6)"),
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        startX + (btnWidth + spacing) * 3, btnY, btnWidth, btnHeight,
        hWnd, (HMENU)ID_TOPMOST_BTN,
        (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE), NULL
    );

    // 设置字体：因为 hNormalFont 可能还没创建，这里先不设置，交给 WM_CREATE 后调用 CreateDynamicFonts
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
    // 按钮字体不再基于窗口高度，将在 CreateDynamicFonts 中直接使用 g_buttonHeight
    sizes.button = 0;   // 未使用
    sizes.source = max(20, min(12, (int)(sizes.time * 0.25)));
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

// 窗口最小尺寸常数（长宽）
const int MIN_WINDOW_WIDTH = 600;
const int MIN_WINDOW_HEIGHT = 400;

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

    hMainWindow = CreateWindowExW(
        WS_EX_APPWINDOW | (g_transparency < 255 ? WS_EX_LAYERED : 0),
        CLASS_NAME,
        L"时间显示器",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
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
        CreateControls(hWnd);
        SetWindowMinSize(hWnd, MIN_WINDOW_WIDTH, MIN_WINDOW_HEIGHT);

        if (!g_noHotKeys) {
            RegisterHotKey(hWnd, 1, 0, VK_F11);
            RegisterHotKey(hWnd, 2, MOD_CONTROL | MOD_SHIFT, 'Q');
            RegisterHotKey(hWnd, 3, 0, VK_F5);
            RegisterHotKey(hWnd, 4, 0, VK_F6);
        }
        // 按钮创建后，再应用字体（确保 g_buttonHeight 已更新）
        CreateDynamicFonts(hWnd);
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
        // DPI改变后需要重新创建控件（更新按钮尺寸）
        CreateControls(hWnd);
        CreateDynamicFonts(hWnd);
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_SIZING: {
        RECT* pRect = (RECT*)lParam;
        int newWidth = pRect->right - pRect->left;
        int newHeight = pRect->bottom - pRect->top;

        if (newWidth < MIN_WINDOW_WIDTH) {
            if (wParam == WMSZ_LEFT || wParam == WMSZ_BOTTOMLEFT || wParam == WMSZ_TOPLEFT)
                pRect->left = pRect->right - MIN_WINDOW_WIDTH;
            else
                pRect->right = pRect->left + MIN_WINDOW_WIDTH;
        }
        if (newHeight < MIN_WINDOW_HEIGHT) {
            if (wParam == WMSZ_TOP || wParam == WMSZ_TOPLEFT || wParam == WMSZ_TOPRIGHT)
                pRect->top = pRect->bottom - MIN_WINDOW_HEIGHT;
            else
                pRect->bottom = pRect->top + MIN_WINDOW_HEIGHT;
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
        // 重新调整按钮布局（按钮已在 WM_CREATE 或 WM_DPICHANGED 中创建）
        if (hFullscreenBtn && hExitBtn && hSyncBtn && hTopmostBtn) {
            RECT clientRect;
            GetClientRect(hWnd, &clientRect);
            UINT dpi = GetWindowDpi(hWnd);
            float scaleFactor = dpi / 96.0f;
            int btnWidth = (int)(105 * scaleFactor);
            int btnHeight = g_buttonHeight;  // 使用已保存的按钮高度
            int spacing = (int)(15 * scaleFactor);
            int btnY = clientRect.bottom - btnHeight - (int)(20 * scaleFactor);
            int totalWidth = btnWidth * 4 + spacing * 3;
            int startX = max(0, (clientRect.right - totalWidth) / 2);

            SetWindowPos(hFullscreenBtn, NULL, startX, btnY, btnWidth, btnHeight, SWP_NOZORDER);
            SetWindowPos(hExitBtn, NULL, startX + btnWidth + spacing, btnY, btnWidth, btnHeight, SWP_NOZORDER);
            SetWindowPos(hSyncBtn, NULL, startX + (btnWidth + spacing) * 2, btnY, btnWidth, btnHeight, SWP_NOZORDER);
            SetWindowPos(hTopmostBtn, NULL, startX + (btnWidth + spacing) * 3, btnY, btnWidth, btnHeight, SWP_NOZORDER);
        }
        // 重绘和字体刷新
        CreateDynamicFonts(hWnd);
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
            RECT sourceRect = clientRect;
            sourceRect.left = 12; sourceRect.top = 8;
            sourceRect.right = clientRect.right - 12;
            sourceRect.bottom = sourceRect.top + 30;
            HGDIOBJ hOldSourceFont = SelectObject(memDC, hSourceFont);
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
        RECT rc = { 0, 0, MIN_WINDOW_WIDTH, MIN_WINDOW_HEIGHT };
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
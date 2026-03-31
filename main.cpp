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

// 窗口过程函数声明
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

// 全局变量
HFONT hLargeFont = nullptr;     // 大字体用于时间显示
HFONT hDateFont = nullptr;      // 日期字体
HFONT hNormalFont = nullptr;    // 按钮字体
HWND hMainWindow = nullptr;     // 主窗口句柄
HWND hFullscreenBtn = nullptr;  // 全屏按钮句柄
HWND hExitBtn = nullptr;        // 退出按钮句柄
HWND hSyncBtn = nullptr;        // NTP同步按钮句柄

std::atomic<bool> g_syncing{ false };   // 同步进行中标志
std::atomic<bool> g_exitFlag{ false };  // 程序退出标志
std::thread g_syncThread;               // 同步线程对象

struct FontSizes {
    int time;
    int date;
    int week;
    int button;
};

// ==================== 函数声明 ====================
std::wstring GetTimeDateString();
void CreateControls(HWND hWnd);
void HandleTouchInput(HWND hWnd, WPARAM wParam, LPARAM lParam);
void CreateDynamicFonts(HWND hWnd);
void StartSync(HWND hWnd);               // 启动异步同步
void SyncThreadProc(HWND hWnd);          // 同步线程函数
FontSizes CalculateFontSizes(HWND hWnd);

// ==================== 函数实现 ====================

// 1. 时间格式化函数
std::wstring GetTimeDateString() {
    SYSTEMTIME st;
    GetLocalTime(&st);

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

// 2. 动态字体创建函数
void CreateDynamicFonts(HWND hWnd) {
    FontSizes sizes = CalculateFontSizes(hWnd);

    // 释放旧字体
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

    // 创建时间字体
    hLargeFont = CreateFontW(
        -sizes.time, 0, 0, 0, FW_BOLD,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"
    );

    // 创建日期字体
    hDateFont = CreateFontW(
        -sizes.date, 0, 0, 0, FW_SEMIBOLD,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"
    );

    // 创建星期字体
    hNormalFont = CreateFontW(
        -sizes.week, 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"
    );

    // 按钮字体单独处理
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

        // 保存按钮字体引用
        if (hNormalFont) DeleteObject(hNormalFont);
        hNormalFont = hBtnFont;
    }
}

// 3. 创建控件函数（三个按钮）
void CreateControls(HWND hWnd) {
    RECT clientRect;
    GetClientRect(hWnd, &clientRect);

    // ===== 获取当前DPI，计算缩放比例 =====
    UINT dpi = GetWindowDpi(hWnd);
    float scaleFactor = dpi / 96.0f;

    // 按钮尺寸基于DPI缩放
    int btnWidth = (int)(120 * scaleFactor);
    int btnHeight = (int)(40 * scaleFactor);
    int spacing = (int)(20 * scaleFactor);
    int btnY = clientRect.bottom - btnHeight - (int)(20 * scaleFactor);

    int totalWidth = btnWidth * 3 + spacing * 2;
    int startX = (clientRect.right - totalWidth) / 2;

    // 全屏按钮
    hFullscreenBtn = CreateWindowW(
        L"BUTTON", L"全屏 (F11)",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        startX, btnY, btnWidth, btnHeight,
        hWnd, (HMENU)ID_FULLSCREEN_BTN,
        (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE), NULL
    );

    // 退出按钮
    hExitBtn = CreateWindowW(
        L"BUTTON", L"退出 (Esc)",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        startX + btnWidth + spacing, btnY, btnWidth, btnHeight,
        hWnd, (HMENU)ID_EXIT_BTN,
        (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE), NULL
    );

    // 同步按钮
    hSyncBtn = CreateWindowW(
        L"BUTTON", L"同步时间",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
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

// 4. 触摸输入处理（不变）
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

// 5. 启动异步同步
void StartSync(HWND hWnd) {
    if (g_syncing) return; // 已经在同步中
    g_syncing = true;

    // 修改按钮文本为“同步中”
    SetWindowTextW(hSyncBtn, L"同步中");

    // 如果已有线程，等待其结束（正常情况下不会发生）
    if (g_syncThread.joinable()) {
        g_syncThread.join();
    }

    // 创建新线程
    g_syncThread = std::thread(SyncThreadProc, hWnd);
}

// 6. 同步线程函数
void SyncThreadProc(HWND hWnd) {
    // 调用NTP同步（可能会阻塞几秒）
    bool success = SyncTimeOnce();

    // 程序未退出时才发送消息
    if (!g_exitFlag) {
        PostMessageW(hWnd, WM_NTP_SYNC_FINISHED, success ? 1 : 0, 0);
    }
}

// 7. 设定字体大小
FontSizes CalculateFontSizes(HWND hWnd) {
    RECT clientRect;
    GetClientRect(hWnd, &clientRect);

    UINT dpi = GetWindowDpi(hWnd);
    float scaleFactor = dpi / 96.0f;

    int windowHeight = clientRect.bottom - clientRect.top;
    int windowWidth = clientRect.right - clientRect.left;

    FontSizes sizes;

    // 时间字体
    int targetTimeHeight = (int)(windowHeight * 0.35);

    // 字体大小应该是显示区域高度的 0.7-0.8 倍
    sizes.time = (int)(targetTimeHeight * 0.75);

    // 限制字体大小范围
    sizes.time = max(40, min(sizes.time, (int)(windowHeight * 0.45)));

    // 日期字体
    sizes.date = (int)(sizes.time * 0.3);
    sizes.date = max(15, min(sizes.date, (int)(windowHeight * 0.12)));

    // 星期字体
    sizes.week = (int)(sizes.time * 0.3);
    sizes.week = max(15, min(sizes.week, (int)(windowHeight * 0.1)));

    // 按钮字体
    int btnBase = 20;
    sizes.button = btnBase;

    return sizes;
}

// 8. 应用程序入口点
int WINAPI wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow) {

    // ===== 添加：主动设置DPI感知 =====
    // 方式1：使用Win10+的PerMonitorV2
    HMODULE hUser32 = LoadLibraryW(L"user32.dll");
    if (hUser32) {
        auto pSetProcessDpiAwarenessContext = (BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT))GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pSetProcessDpiAwarenessContext) {
            pSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
        FreeLibrary(hUser32);
    }
    // 备用：使用Win8.1方式
    else {
        SetProcessDPIAware(); // 旧版API
    }
    // ====

    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // 初始化通用控件
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    // 注册窗口类
    const wchar_t CLASS_NAME[] = L"Windows11StyleClockClass";

    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = CreateSolidBrush(RGB(10, 10, 20));
    wcex.lpszClassName = CLASS_NAME;
    wcex.hIcon = LoadIconW(hInstance, MAKEINTRESOURCE(101));
    wcex.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCE(101));

    if (!RegisterClassExW(&wcex)) {
        MessageBoxW(nullptr, L"窗口类注册失败！", L"错误", MB_ICONERROR);
        return 1;
    }

    // 创建主窗口
    hMainWindow = CreateWindowExW(
        WS_EX_APPWINDOW,
        CLASS_NAME,
        L"全屏时间显示器",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        nullptr, nullptr, hInstance, nullptr);

    if (!hMainWindow) {
        MessageBoxW(nullptr, L"主窗口创建失败！", L"错误", MB_ICONERROR);
        return 1;
    }

    // 设置窗口圆角
    DWM_WINDOW_CORNER_PREFERENCE cornerPreference = DWMWCP_ROUND;
    DwmSetWindowAttribute(hMainWindow, DWMWA_WINDOW_CORNER_PREFERENCE,
        &cornerPreference, sizeof(cornerPreference));

    // 注册触控支持
    RegisterTouchWindow(hMainWindow, TWF_FINETOUCH);

    // 初始化NTP客户端
    if (!InitializeNTPClient()) {
        // 初始化失败，按钮可能不可用，但程序继续运行
        OutputDebugStringW(L"NTP客户端初始化失败\n");
    }

    // 初始化动态字体
    CreateDynamicFonts(hMainWindow);

    // 初始化全屏模块
    InitializeFullscreenModule(hMainWindow);

    // 显示并更新窗口
    ShowWindow(hMainWindow, nCmdShow);
    UpdateWindow(hMainWindow);

    // 启动 NTP 自动同步（后台定时同步，每小时一次）
    StartAutoSync();

    // 可选：启动后立即手动同步一次时间（异步，不阻塞UI）
    StartSync(hMainWindow);  // 使用已有的异步同步函数

    // 设置定时器（每秒刷新时间显示）
    SetTimer(hMainWindow, 1, 1000, nullptr);

    // 消息循环
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // 清理资源
    if (hLargeFont) DeleteObject(hLargeFont);
    if (hDateFont) DeleteObject(hDateFont);
    if (hNormalFont) DeleteObject(hNormalFont);

    // 等待同步线程结束（如果还在运行）
    g_exitFlag = true;
    if (g_syncThread.joinable()) {
        g_syncThread.join();
    }

    // 清理NTP客户端
    CleanupNTPClient();

    CleanupFullscreenModule();

    return (int)msg.wParam;
}

// 8. 窗口过程函数
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        CreateControls(hWnd);
        break;
    }

    case WM_DPICHANGED: {
        // wParam的低16位是新的DPI值
        UINT newDpi = LOWORD(wParam);

        // lParam包含系统建议的新窗口位置和大小
        RECT* newRect = (RECT*)lParam;

        // 调整窗口大小到建议的尺寸
        SetWindowPos(hWnd, NULL,
            newRect->left, newRect->top,
            newRect->right - newRect->left,
            newRect->bottom - newRect->top,
            SWP_NOZORDER | SWP_NOACTIVATE);

        // 重新创建字体以适应新的DPI
        CreateDynamicFonts(hWnd);

        // 强制重绘
        InvalidateRect(hWnd, NULL, TRUE);
        return 0;
    }
    // ===== DPI处理结束 =====

    case WM_SIZE: {
        // 重新创建字体以适应新窗口大小
        CreateDynamicFonts(hWnd);

        // 重绘窗口
        InvalidateRect(hWnd, NULL, TRUE);

        // 更新三个按钮的位置
        if (hFullscreenBtn && hExitBtn && hSyncBtn) {
            RECT clientRect;
            GetClientRect(hWnd, &clientRect);

            // ===== 使用DPI缩放计算按钮位置 =====
            UINT dpi = GetWindowDpi(hWnd);
            float scaleFactor = dpi / 96.0f;

            int btnWidth = (int)(120 * scaleFactor);
            int btnHeight = (int)(40 * scaleFactor);
            int btnY = clientRect.bottom - btnHeight - (int)(20 * scaleFactor);
            int spacing = (int)(20 * scaleFactor);
            int totalWidth = btnWidth * 3 + spacing * 2;
            int startX = max(0, (clientRect.right - totalWidth) / 2);
            // =================================

            SetWindowPos(hFullscreenBtn, NULL,
                startX, btnY, btnWidth, btnHeight, SWP_NOZORDER);
            SetWindowPos(hExitBtn, NULL,
                startX + btnWidth + spacing, btnY, btnWidth, btnHeight, SWP_NOZORDER);
            SetWindowPos(hSyncBtn, NULL,
                startX + (btnWidth + spacing) * 2, btnY, btnWidth, btnHeight, SWP_NOZORDER);
        }
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT clientRect;
        GetClientRect(hWnd, &clientRect);

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

        // 绘制时间
        RECT timeRect = clientRect;
        timeRect.top = startY;
        timeRect.bottom = startY + timeHeight;

        HGDIOBJ hOldFont = SelectObject(hdc, hLargeFont);
        SetTextColor(hdc, RGB(220, 220, 255));
        SetBkMode(hdc, TRANSPARENT);

        DrawTextW(hdc, timeStr.c_str(), -1, &timeRect,
            DT_CENTER | DT_VCENTER | DT_NOPREFIX | DT_SINGLELINE);

        // 绘制日期
        RECT dateRect = clientRect;
        dateRect.top = startY + timeHeight + 10;
        dateRect.bottom = dateRect.top + dateHeight;

        HGDIOBJ hOldDateFont = SelectObject(hdc, hDateFont);
        SetTextColor(hdc, RGB(200, 200, 240));

        DrawTextW(hdc, dateStr.c_str(), -1, &dateRect,
            DT_CENTER | DT_VCENTER | DT_NOPREFIX | DT_SINGLELINE);

        // 绘制星期
        RECT weekRect = clientRect;
        weekRect.top = dateRect.bottom + 10;
        weekRect.bottom = weekRect.top + weekHeight;

        SetTextColor(hdc, RGB(180, 180, 220));
        DrawTextW(hdc, weekStr.c_str(), -1, &weekRect,
            DT_CENTER | DT_VCENTER | DT_NOPREFIX | DT_SINGLELINE);

        SelectObject(hdc, hOldDateFont);
        SelectObject(hdc, hOldFont);

        EndPaint(hWnd, &ps);
        break;
    }

    case WM_TIMER: {
        if (wParam == 1) {
            InvalidateRect(hWnd, NULL, TRUE);
        }
        break;
    }

    case WM_COMMAND: {
        WORD ctrlId = LOWORD(wParam);
        if (ctrlId == ID_FULLSCREEN_BTN) {
            ToggleFullscreen(hWnd);
            SetWindowTextW(hFullscreenBtn, IsFullscreen() ? L"窗口化 (F11)" : L"全屏 (F11)");
        }
        else if (ctrlId == ID_EXIT_BTN) {
            PostMessage(hWnd, WM_CLOSE, 0, 0);
        }
        else if (ctrlId == ID_SYNC_BTN) {
            StartSync(hWnd);  // 手动同步
        }
        break;
    }

    case WM_KEYDOWN: {
        if (wParam == VK_F11) {
            ToggleFullscreen(hWnd);
            SetWindowTextW(hFullscreenBtn, IsFullscreen() ? L"窗口化 (F11)" : L"全屏 (F11)");
        }
        else if (wParam == VK_ESCAPE) {
            PostMessage(hWnd, WM_CLOSE, 0, 0);
        }
        break;
    }

    case WM_NCLBUTTONDBLCLK: {
        if (HandleNcLButtonDblClk(hWnd, wParam)) {
            SetWindowTextW(hFullscreenBtn, IsFullscreen() ? L"窗口化 (F11)" : L"全屏 (F11)");
        }
        break;
    }

    case WM_GETMINMAXINFO: {
        LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;
        lpMMI->ptMinTrackSize.x = 500;
        lpMMI->ptMinTrackSize.y = 400;
        HandleGetMinMaxInfo(lParam);
        break;
    }

    case WM_TOUCH: {
        HandleTouchInput(hWnd, wParam, lParam);
        return 0;
    }

    case WM_NTP_SYNC_FINISHED: {
        // 同步完成，wParam为1表示成功，0表示失败
        if (wParam == 1) {
            SetWindowTextW(hSyncBtn, L"同步成功");
        }
        else {
            SetWindowTextW(hSyncBtn, L"同步失败");
        }
        g_syncing = false;
        break;
    }

    case WM_DESTROY: {
        KillTimer(hWnd, 1);
        PostQuitMessage(0);
        break;
    }

    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }

    return 0;
}
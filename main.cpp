// main.cpp
// Windows 11风格时间显示主程序

#include <windows.h>
#include <dwmapi.h>
#include <string>
#include <commctrl.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")

// 包含全屏模块头文件
#include "fullscreen.h"

// 窗口过程函数声明
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

// 全局变量
HFONT hLargeFont = nullptr;     // 大字体用于时间显示
HFONT hDateFont = nullptr;      // 日期字体
HFONT hNormalFont = nullptr;    // 按钮字体
HWND hMainWindow = nullptr;     // 主窗口句柄
HWND hFullscreenBtn = nullptr;  // 全屏按钮句柄
HWND hExitBtn = nullptr;        // 退出按钮句柄

// ==================== 函数声明 ====================
std::wstring GetTimeDateString();
void CreateControls(HWND hWnd);
void HandleTouchInput(HWND hWnd, WPARAM wParam, LPARAM lParam);
void CreateDynamicFonts(HWND hWnd);  // 👈 新增：动态字体创建函数声明

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

// 2. 👈 新增：动态字体创建函数（放在这里）
void CreateDynamicFonts(HWND hWnd) {
    // 获取客户区大小
    RECT clientRect;
    GetClientRect(hWnd, &clientRect);

    // 计算字体大小：时间字体为窗口高度的 1/5，日期字体为窗口高度的 1/12
    int windowHeight = clientRect.bottom - clientRect.top;
    int timeFontSize = max(36, min(120, windowHeight / 5));  // 范围 36-120
    int dateFontSize = max(16, min(48, windowHeight / 12));  // 范围 16-48

    // 删除旧字体
    if (hLargeFont) {
        DeleteObject(hLargeFont);
        hLargeFont = nullptr;
    }
    if (hDateFont) {
        DeleteObject(hDateFont);
        hDateFont = nullptr;
    }

    // 创建新的大字体（时间）
    hLargeFont = CreateFontW(
        -timeFontSize,                   // 动态字体高度
        0, 0, 0, FW_SEMIBOLD,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH,
        L"Segoe UI"
    );

    // 创建新的日期字体
    hDateFont = CreateFontW(
        -dateFontSize,                    // 动态字体高度
        0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH,
        L"Segoe UI"
    );

    // 按钮字体保持固定大小
    if (!hNormalFont) {
        hNormalFont = CreateFontW(
            -14, 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Segoe UI"
        );
    }

    // 更新按钮字体
    if (hFullscreenBtn && hExitBtn && hNormalFont) {
        SendMessageW(hFullscreenBtn, WM_SETFONT, (WPARAM)hNormalFont, TRUE);
        SendMessageW(hExitBtn, WM_SETFONT, (WPARAM)hNormalFont, TRUE);
    }
}

// 3. 创建控件函数
void CreateControls(HWND hWnd) {
    RECT clientRect;
    GetClientRect(hWnd, &clientRect);

    int btnWidth = 120;
    int btnHeight = 40;
    int btnY = clientRect.bottom - btnHeight - 20;
    int totalWidth = btnWidth * 2 + 30;
    int startX = (clientRect.right - totalWidth) / 2;

    hFullscreenBtn = CreateWindowW(
        L"BUTTON", L"全屏 (F11)",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        startX, btnY, btnWidth, btnHeight,
        hWnd, (HMENU)1001,
        (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE), NULL
    );

    hExitBtn = CreateWindowW(
        L"BUTTON", L"退出 (Esc)",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        startX + btnWidth + 30, btnY, btnWidth, btnHeight,
        hWnd, (HMENU)1002,
        (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE), NULL
    );

    if (hNormalFont && hFullscreenBtn && hExitBtn) {
        SendMessageW(hFullscreenBtn, WM_SETFONT, (WPARAM)hNormalFont, TRUE);
        SendMessageW(hExitBtn, WM_SETFONT, (WPARAM)hNormalFont, TRUE);
    }
}

// 4. 触摸输入处理函数
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

// 5. 应用程序入口点
int WINAPI wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow) {

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
    wcex.hIcon = LoadIcon(hInstance, IDI_APPLICATION);
    wcex.hIconSm = LoadIcon(hInstance, IDI_APPLICATION);

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

    // 👈 初始化动态字体（根据初始窗口大小）
    CreateDynamicFonts(hMainWindow);

    // 初始化全屏模块
    InitializeFullscreenModule(hMainWindow);

    // 显示并更新窗口
    ShowWindow(hMainWindow, nCmdShow);
    UpdateWindow(hMainWindow);

    // 设置定时器
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

    CleanupFullscreenModule();

    return (int)msg.wParam;
}

// 6. 窗口过程函数
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        CreateControls(hWnd);
        break;
    }

    case WM_SIZE: {
        // 👈 窗口大小改变时重新创建字体
        CreateDynamicFonts(hWnd);

        // 重绘窗口
        InvalidateRect(hWnd, NULL, TRUE);

        // 更新按钮位置
        if (hFullscreenBtn && hExitBtn) {
            RECT clientRect;
            GetClientRect(hWnd, &clientRect);

            int btnWidth = 120;
            int btnHeight = 40;
            int btnY = clientRect.bottom - btnHeight - 20;
            int totalWidth = btnWidth * 2 + 30;
            int startX = (clientRect.right - totalWidth) / 2;

            SetWindowPos(hFullscreenBtn, NULL,
                startX, btnY, btnWidth, btnHeight, SWP_NOZORDER);
            SetWindowPos(hExitBtn, NULL,
                startX + btnWidth + 30, btnY, btnWidth, btnHeight, SWP_NOZORDER);
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

        // 动态布局计算
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
        if (ctrlId == 1001) {
            ToggleFullscreen(hWnd);
            SetWindowTextW(hFullscreenBtn, IsFullscreen() ? L"窗口化 (F11)" : L"全屏 (F11)");
        }
        else if (ctrlId == 1002) {
            PostMessage(hWnd, WM_CLOSE, 0, 0);
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
// main.cpp
// Windows 11风格时间显示主程序
// 支持触控交互、定时刷新和全屏切换功能

#include <windows.h>
#include <dwmapi.h>
#include <string>
#include <commctrl.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")

// 包含全屏模块
#include "fullscreen.cpp"

// 窗口过程函数声明
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

// 全局变量
HFONT hLargeFont = nullptr;     // 大字体用于时间显示
HFONT hNormalFont = nullptr;    // 正常字体用于按钮
HWND hMainWindow = nullptr;     // 主窗口句柄
HWND hFullscreenBtn = nullptr;  // 全屏按钮句柄
HWND hExitBtn = nullptr;        // 退出按钮句柄

// 时间格式化函数
std::wstring GetTimeDateString() {
    SYSTEMTIME st;
    GetLocalTime(&st);

    // 格式: HH:mm:ss\nYYYY-MM-DD
    wchar_t timeStr[64];
    wsprintfW(timeStr, L"%02d:%02d:%02d\n%04d-%02d-%02d",
        st.wHour, st.wMinute, st.wSecond,
        st.wYear, st.wMonth, st.wDay);

    // 添加星期信息
    const wchar_t* weekdays[] = {
        L"星期日", L"星期一", L"星期二", L"星期三",
        L"星期四", L"星期五", L"星期六"
    };

    std::wstring result = timeStr;
    result += L"\n";
    result += weekdays[st.wDayOfWeek];

    return result;
}

// 创建控件函数
void CreateControls(HWND hWnd) {
    // 创建全屏切换按钮
    hFullscreenBtn = CreateWindowW(
        L"BUTTON",                         // 按钮类
        L"全屏 (F11)",                     // 按钮文本
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT, // 样式
        20, 320,                           // 位置 x, y
        120, 40,                           // 宽度, 高度
        hWnd,                              // 父窗口
        (HMENU)1001,                       // 控件ID
        (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE),
        NULL
    );

    // 创建退出按钮
    hExitBtn = CreateWindowW(
        L"BUTTON",                         // 按钮类
        L"退出 (Esc)",                     // 按钮文本
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        170, 320,                          // 位置 x, y
        120, 40,                           // 宽度, 高度
        hWnd,                              // 父窗口
        (HMENU)1002,                       // 控件ID
        (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE),
        NULL
    );

    // 设置按钮字体
    if (hNormalFont && hFullscreenBtn && hExitBtn) {
        SendMessageW(hFullscreenBtn, WM_SETFONT, (WPARAM)hNormalFont, TRUE);
        SendMessageW(hExitBtn, WM_SETFONT, (WPARAM)hNormalFont, TRUE);
    }
}

// 触摸输入处理函数
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

                // 检测双击手势（简化实现）
                if (ti.dwFlags & TOUCHEVENTF_DOWN) {
                    static DWORD lastTouchTime = 0;
                    static POINT lastTouchPoint = { 0, 0 };
                    DWORD currentTime = GetTickCount();
                    POINT currentPoint = ptInput;

                    if ((currentTime - lastTouchTime) < 500 && // 500ms内
                        abs(currentPoint.x - lastTouchPoint.x) < 50 &&
                        abs(currentPoint.y - lastTouchPoint.y) < 50) {
                        // 视为双击，切换全屏
                        ToggleFullscreen(hWnd);
                        // 更新按钮文本
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

// 应用程序入口点
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
    LPWSTR lpCmdLine, int nCmdShow) {
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
    wcex.hbrBackground = CreateSolidBrush(RGB(10, 10, 20)); // 深色背景，类似数字时钟
    wcex.lpszClassName = CLASS_NAME;
    wcex.hIcon = LoadIcon(hInstance, IDI_APPLICATION);
    wcex.hIconSm = LoadIcon(hInstance, IDI_APPLICATION);

    if (!RegisterClassExW(&wcex)) {
        MessageBoxW(nullptr, L"窗口类注册失败！", L"错误", MB_ICONERROR);
        return 1;
    }

    // 计算窗口居中位置
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int windowWidth = 800;
    int windowHeight = 600;
    int x = (screenWidth - windowWidth) / 2;
    int y = (screenHeight - windowHeight) / 2;

    // 创建主窗口
    hMainWindow = CreateWindowExW(
        WS_EX_APPWINDOW,
        CLASS_NAME,
        L"全屏时间显示器",
        WS_OVERLAPPEDWINDOW,
        x, y, windowWidth, windowHeight,
        nullptr, nullptr, hInstance, nullptr);

    if (!hMainWindow) {
        MessageBoxW(nullptr, L"主窗口创建失败！", L"错误", MB_ICONERROR);
        return 1;
    }

    // 设置窗口圆角（Windows 11特性）
    DWM_WINDOW_CORNER_PREFERENCE cornerPreference = DWMWCP_ROUND;
    DwmSetWindowAttribute(hMainWindow, DWMWA_WINDOW_CORNER_PREFERENCE,
        &cornerPreference, sizeof(cornerPreference));

    // 注册触控支持
    RegisterTouchWindow(hMainWindow, TWF_FINETOUCH); // 启用精细触控减少延迟

    // 创建大字体用于时间显示（Segoe UI Semibold）
    hLargeFont = CreateFontW(
        -72,                            // 字体高度（大号）
        0,                              // 字符平均宽度
        0,                              // 角度偏移
        0,                              // 基线到基线距离
        FW_SEMIBOLD,                   // 字重（半粗）
        FALSE,                         // 是否斜体
        FALSE,                         // 是否下划线
        FALSE,                         // 是否删除线
        DEFAULT_CHARSET,               // 字符集
        OUT_DEFAULT_PRECIS,            // 输出精度
        CLIP_DEFAULT_PRECIS,           // 剪裁精度
        CLEARTYPE_QUALITY,             // 输出质量（启用ClearType优化）
        DEFAULT_PITCH,                 // 字族
        L"Segoe UI"                    // 字体名称
    );

    // 创建正常字体用于按钮
    hNormalFont = CreateFontW(
        -14,                            // 字体高度
        0,                              // 字符平均宽度
        0,                              // 角度偏移
        0,                              // 基线到基线距离
        FW_NORMAL,                     // 字重
        FALSE,                         // 是否斜体
        FALSE,                         // 是否下划线
        FALSE,                         // 是否删除线
        DEFAULT_CHARSET,               // 字符集
        OUT_DEFAULT_PRECIS,            // 输出精度
        CLIP_DEFAULT_PRECIS,           // 剪裁精度
        CLEARTYPE_QUALITY,             // 输出质量（启用ClearType优化）
        DEFAULT_PITCH,                 // 字族
        L"Segoe UI"                    // 字体名称
    );

    // 初始化全屏模块
    InitializeFullscreenModule(hMainWindow);

    // 显示并更新窗口
    ShowWindow(hMainWindow, nCmdShow);
    UpdateWindow(hMainWindow);

    // 设置定时器，每秒更新一次显示
    SetTimer(hMainWindow, 1, 1000, nullptr);

    // Win32消息循环
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // 清理资源
    if (hLargeFont) {
        DeleteObject(hLargeFont);
    }
    if (hNormalFont) {
        DeleteObject(hNormalFont);
    }

    // 清理全屏模块
    CleanupFullscreenModule();

    return (int)msg.wParam;
}

// 窗口过程函数
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        // 创建控件
        CreateControls(hWnd);
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        // 使用大字体绘制时间文本
        HGDIOBJ hOldFont = SelectObject(hdc, hLargeFont);

        // 设置文本颜色（亮色）和背景模式
        SetTextColor(hdc, RGB(220, 220, 255)); // 浅蓝色文字
        SetBkMode(hdc, TRANSPARENT);

        // 获取客户区大小
        RECT clientRect;
        GetClientRect(hWnd, &clientRect);

        // 创建时间文本区域（占据窗口主要区域）
        RECT timeRect = clientRect;
        timeRect.bottom = 300; // 时间显示区域高度

        // 获取当前时间日期字符串并居中绘制
        std::wstring timeDateStr = GetTimeDateString();
        DrawTextW(hdc, timeDateStr.c_str(), -1, &timeRect,
            DT_CENTER | DT_VCENTER | DT_NOPREFIX | DT_SINGLELINE);

        // 在时间下方添加日期和星期的副标题
        RECT dateRect = clientRect;
        dateRect.top = 300;
        dateRect.bottom = 350;

        // 获取系统时间
        SYSTEMTIME st;
        GetLocalTime(&st);

        // 格式化日期字符串
        const wchar_t* weekdays[] = {
            L"星期日", L"星期一", L"星期二", L"星期三",
            L"星期四", L"星期五", L"星期六"
        };
        wchar_t dateStr[128];
        wsprintfW(dateStr, L"%04d年%02d月%02d日  %s",
            st.wYear, st.wMonth, st.wDay, weekdays[st.wDayOfWeek]);

        // 使用稍小的字体
        HFONT hDateFont = CreateFontW(
            -24,                            // 字体高度
            0,                              // 字符平均宽度
            0,                              // 角度偏移
            0,                              // 基线到基线距离
            FW_NORMAL,                     // 字重
            FALSE,                         // 是否斜体
            FALSE,                         // 是否下划线
            FALSE,                         // 是否删除线
            DEFAULT_CHARSET,               // 字符集
            OUT_DEFAULT_PRECIS,            // 输出精度
            CLIP_DEFAULT_PRECIS,           // 剪裁精度
            CLEARTYPE_QUALITY,             // 输出质量
            DEFAULT_PITCH,                 // 字族
            L"Segoe UI"                    // 字体名称
        );

        HGDIOBJ hOldDateFont = SelectObject(hdc, hDateFont);
        SetTextColor(hdc, RGB(180, 180, 220)); // 稍暗的颜色
        DrawTextW(hdc, dateStr, -1, &dateRect,
            DT_CENTER | DT_VCENTER | DT_NOPREFIX | DT_SINGLELINE);
        SelectObject(hdc, hOldDateFont);
        DeleteObject(hDateFont);

        // 恢复原始字体
        SelectObject(hdc, hOldFont);
        EndPaint(hWnd, &ps);
        break;
    }

    case WM_SIZE: {
        // 窗口大小改变时重绘
        InvalidateRect(hWnd, NULL, TRUE);
        break;
    }

    case WM_TIMER: {
        // 定时器触发，刷新显示
        if (wParam == 1) {
            InvalidateRect(hWnd, NULL, TRUE);
        }
        break;
    }

    case WM_COMMAND: {
        // 处理按钮点击
        WORD ctrlId = LOWORD(wParam);

        if (ctrlId == 1001) { // 全屏按钮
            ToggleFullscreen(hWnd);
            // 更新按钮文本
            SetWindowTextW(hFullscreenBtn, IsFullscreen() ? L"窗口化 (F11)" : L"全屏 (F11)");
        }
        else if (ctrlId == 1002) { // 退出按钮
            PostMessage(hWnd, WM_CLOSE, 0, 0);
        }
        break;
    }

    case WM_KEYDOWN: {
        // 处理快捷键
        if (wParam == VK_F11) { // F11 切换全屏
            ToggleFullscreen(hWnd);
            SetWindowTextW(hFullscreenBtn, IsFullscreen() ? L"窗口化 (F11)" : L"全屏 (F11)");
        }
        else if (wParam == VK_ESCAPE) { // ESC 退出
            PostMessage(hWnd, WM_CLOSE, 0, 0);
        }
        break;
    }

    case WM_NCLBUTTONDBLCLK: {
        // 标题栏双击切换全屏
        if (HandleNcLButtonDblClk(hWnd, wParam)) {
            SetWindowTextW(hFullscreenBtn, IsFullscreen() ? L"窗口化 (F11)" : L"全屏 (F11)");
        }
        break;
    }

    case WM_GETMINMAXINFO: {
        // 限制窗口最小大小
        HandleGetMinMaxInfo(lParam);
        break;
    }

    case WM_TOUCH: {
        // 处理触控输入
        HandleTouchInput(hWnd, wParam, lParam);
        return 0; // 已处理
    }

    case WM_DESTROY: {
        // 清理定时器
        KillTimer(hWnd, 1);
        PostQuitMessage(0);
        break;
    }

    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }

    return 0;
}
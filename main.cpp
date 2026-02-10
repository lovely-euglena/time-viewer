// main.cpp
// Windows 11风格时间显示主程序
// 支持触控交互、定时刷新和全屏切换功能

#include <windows.h>
#include <dwmapi.h>
#include <string>

#pragma comment(lib, "dwmapi.lib")

// 窗口过程函数声明
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

// 全局变量
HFONT hFont = nullptr; // 字体句柄
HWND hMainWindow = nullptr; // 主窗口句柄

// 时间格式化函数
std::wstring GetTimeDateString() {
    SYSTEMTIME st;
    GetLocalTime(&st);

    // 格式: HH:mm:ss\nYYYY-MM-DD 星期X
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

                // 这里可以添加触摸点的视觉反馈逻辑
                // 例如：在触摸位置绘制高亮区域

                // 检测双击手势（简化实现）
                if (ti.dwFlags & TOUCHEVENTF_DOWN) {
                    static DWORD lastTouchTime = 0;
                    static POINT lastTouchPoint = { 0, 0 };
                    DWORD currentTime = GetTickCount();
                    POINT currentPoint = ptInput;

                    if ((currentTime - lastTouchTime) < 500 && // 500ms内
                        abs(currentPoint.x - lastTouchPoint.x) < 50 &&
                        abs(currentPoint.y - lastTouchPoint.y) < 50) {
                        // 视为双击，触发窗口刷新
                        InvalidateRect(hWnd, NULL, TRUE);
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

    // 注册窗口类
    const wchar_t CLASS_NAME[] = L"Windows11StyleClockClass";

    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = CreateSolidBrush(RGB(243, 243, 243)); // Windows 11浅色主题背景
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
    int windowWidth = 400;
    int windowHeight = 200;
    int x = (screenWidth - windowWidth) / 2;
    int y = (screenHeight - windowHeight) / 2;

    // 创建主窗口
    hMainWindow = CreateWindowExW(
        WS_EX_APPWINDOW,
        CLASS_NAME,
        L"Windows 11风格时间显示",
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

    // 创建Segoe UI字体（Windows 11标准字体）
    hFont = CreateFontW(
        -16,                            // 字体高度
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
    if (hFont) {
        DeleteObject(hFont);
    }

    return (int)msg.wParam;
}

// 窗口过程函数
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        // 在窗口创建时进行初始化
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        // 使用Segoe UI字体绘制文本
        HGDIOBJ hOldFont = SelectObject(hdc, hFont);

        // 设置文本颜色和背景模式
        SetTextColor(hdc, RGB(32, 32, 32)); // 深灰色文字
        SetBkMode(hdc, TRANSPARENT);

        // 获取客户区大小
        RECT clientRect;
        GetClientRect(hWnd, &clientRect);

        // 应用16px内边距（符合Windows 11设计规范）
        RECT textRect = clientRect;
        textRect.left += 16;
        textRect.top += 16;
        textRect.right -= 16;
        textRect.bottom -= 16;

        // 获取当前时间日期字符串并绘制
        std::wstring timeDateStr = GetTimeDateString();
        DrawTextW(hdc, timeDateStr.c_str(), -1, &textRect,
            DT_LEFT | DT_TOP | DT_NOPREFIX);

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
// fullscreen.cpp
// 全屏切换功能模块实现文件
// 提供F11快捷键和标题栏双击切换全屏功能，支持真正的全屏覆盖（含任务栏）

#include "fullscreen.h"
#include <windows.h>
#include <winuser.h>

// 静态变量用于保存全屏状态和窗口原始信息
static bool g_bFullscreen = false;                    // 全屏状态标志
static WINDOWPLACEMENT g_wpPrev = { sizeof(g_wpPrev) }; // 保存窗口原始位置和大小
static LONG g_lStyle = 0;                             // 保存窗口原始样式
static LONG g_lExStyle = 0;                           // 保存窗口扩展样式

/**
 * 切换全屏状态的主函数
 * @param hWnd: 窗口句柄
 * @return 成功返回true，失败返回false
 */
bool ToggleFullscreen(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd))
        return false;

    RECT rcMonitorArea;
    MONITORINFO mi = { sizeof(mi) };

    // 获取窗口所在的显示器完整区域
    HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    if (hMonitor && GetMonitorInfo(hMonitor, &mi)) {
        rcMonitorArea = mi.rcMonitor;
    }
    else {
        rcMonitorArea.left = 0;
        rcMonitorArea.top = 0;
        rcMonitorArea.right = GetSystemMetrics(SM_CXSCREEN);
        rcMonitorArea.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    if (!g_bFullscreen) {
        // 进入全屏模式
        // 保存当前窗口状态
        g_wpPrev.length = sizeof(g_wpPrev);
        if (!GetWindowPlacement(hWnd, &g_wpPrev))
            return false;

        // 保存原始窗口样式
        g_lStyle = GetWindowLong(hWnd, GWL_STYLE);
        g_lExStyle = GetWindowLong(hWnd, GWL_EXSTYLE);

        // 移除窗口边框和标题栏样式
        LONG newStyle = g_lStyle & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
        SetWindowLong(hWnd, GWL_STYLE, newStyle);

        // 添加置顶扩展样式
        SetWindowLong(hWnd, GWL_EXSTYLE, g_lExStyle | WS_EX_TOPMOST);

        // 调整窗口大小至完整显示器区域
        SetWindowPos(hWnd, HWND_TOPMOST,
            rcMonitorArea.left, rcMonitorArea.top,
            rcMonitorArea.right - rcMonitorArea.left,
            rcMonitorArea.bottom - rcMonitorArea.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);

        g_bFullscreen = true;
    }
    else {
        // 退出全屏模式
        // 恢复原始窗口样式
        SetWindowLong(hWnd, GWL_STYLE, g_lStyle);
        SetWindowLong(hWnd, GWL_EXSTYLE, g_lExStyle);

        // 恢复原始窗口位置和大小
        SetWindowPlacement(hWnd, &g_wpPrev);

        // 取消置顶
        SetWindowPos(hWnd, HWND_NOTOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);

        // ===== 添加：恢复后确保满足最小尺寸和比例限制 =====
        RECT rc;
        GetWindowRect(hWnd, &rc);
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;

        // 检查最小尺寸
        int minWidth = 500;
        int minHeight = 300;

        // 需要加上边框计算实际客户区最小尺寸
        RECT minRect = { 0, 0, minWidth, minHeight };
        AdjustWindowRectEx(&minRect, g_lStyle, FALSE, g_lExStyle);

        int actualMinWidth = minRect.right - minRect.left;
        int actualMinHeight = minRect.bottom - minRect.top;

        if (width < actualMinWidth || height < actualMinHeight) {
            width = max(width, actualMinWidth);
            height = max(height, actualMinHeight);
            SetWindowPos(hWnd, NULL, 0, 0, width, height,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }

        // 检查宽高比例（宽:高 >= 6:5）
        float minRatio = 6.0f / 5.0f;  // 1.2
        float currentRatio = (float)width / height;

        if (currentRatio < minRatio) {
            int newWidth = (int)(height * minRatio);
            SetWindowPos(hWnd, NULL, 0, 0, newWidth, height,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }

        g_bFullscreen = false;
    }

    return true;
}

/**
 * 检查当前是否处于全屏状态
 * @return 当前为全屏状态返回true，否则返回false
 */
bool IsFullscreen()
{
    return g_bFullscreen;
}

/**
 * 获取当前全屏状态
 * @return 返回全屏状态枚举值：0=窗口化，1=全屏
 */
int GetFullscreenState()
{
    return g_bFullscreen ? 1 : 0;
}

/**
 * 处理WM_KEYDOWN消息以支持F11快捷键切换全屏
 * @param hWnd: 窗口句柄
 * @param wParam: 虚拟键码
 * @return 如果已处理该消息返回true，否则返回false
 */
bool HandleKeyDownForFullscreen(HWND hWnd, WPARAM wParam)
{
    if (wParam == VK_F11)
    {
        ToggleFullscreen(hWnd);
        return true; // 消息已处理
    }
    return false;
}

/**
 * 处理WM_NCLBUTTONDBLCLK消息以支持标题栏双击切换全屏
 * @param hWnd: 窗口句柄
 * @param wParam: 鼠标位置代码（如HT_CAPTION）
 * @return 如果已处理该消息返回true，否则返回false
 */
bool HandleNcLButtonDblClk(HWND hWnd, WPARAM wParam)
{
    // 检查双击位置是否为标题栏
    if (wParam == HTCAPTION)
    {
        ToggleFullscreen(hWnd);
        return true;
    }
    return false;
}

/**
 * 处理WM_GETMINMAXINFO消息以限制窗口最小大小
 * @param lParam: MINMAXINFO结构指针
 */
void HandleGetMinMaxInfo(LPARAM lParam)
{
    LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;

    // 设置最小跟踪大小，避免窗口过小
    lpMMI->ptMinTrackSize.x = 500;  // 最小宽度
    lpMMI->ptMinTrackSize.y = 400;  // 最小高度

    if (g_bFullscreen)
    {
        // 全屏状态下可以设置更小的限制（但实际上全屏时不会用到）
        // 这里保持和普通窗口一致或更小
    }
}

/**
 * 全屏模块初始化函数
 * 可在程序启动时调用以初始化全屏模块状态
 * @param hWnd: 主窗口句柄（可选）
 */
void InitializeFullscreenModule(HWND hWnd)
{
    // 初始化状态变量
    g_bFullscreen = false;
    g_wpPrev.length = sizeof(g_wpPrev);
    g_lStyle = 0;
    g_lExStyle = 0;

    // 如果提供了窗口句柄，预先获取其初始状态
    if (hWnd && IsWindow(hWnd))
    {
        GetWindowPlacement(hWnd, &g_wpPrev);
    }
}

/**
 * 清理全屏模块资源
 * 在程序退出前调用
 */
void CleanupFullscreenModule()
{
    // 当前无动态分配资源需要清理
    // 可在此添加其他清理逻辑
}

// fullscreen.cpp - 在文件末尾添加

/**
 * 获取窗口当前所在的DPI值
 * @param hWnd: 窗口句柄
 * @return DPI值（如96, 120, 144, 192等）
 */
UINT GetWindowDpi(HWND hWnd) {
    UINT dpi = 96; // 默认96 DPI

    if (!hWnd || !IsWindow(hWnd)) {
        return dpi;
    }

    // 方法1：使用 GetDpiForWindow (Windows 10 1607+)
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        auto pGetDpiForWindow = (UINT(WINAPI*)(HWND))GetProcAddress(hUser32, "GetDpiForWindow");
        if (pGetDpiForWindow) {
            dpi = pGetDpiForWindow(hWnd);
            if (dpi > 0) {
                return dpi;
            }
        }
    }

    // 方法2：使用 GetDpiForMonitor (Windows 8.1+)
    HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    if (hMonitor) {
        HMODULE hShcore = LoadLibraryW(L"shcore.dll");
        if (hShcore) {
            auto pGetDpiForMonitor = (HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*))GetProcAddress(hShcore, "GetDpiForMonitor");
            if (pGetDpiForMonitor) {
                UINT dpiX, dpiY;
                if (SUCCEEDED(pGetDpiForMonitor(hMonitor, 0, &dpiX, &dpiY))) {
                    dpi = dpiX;
                }
            }
            FreeLibrary(hShcore);
        }
    }

    return dpi;
}

// fullscreen.cpp - 在文件末尾添加函数实现

/**
 * 设置窗口的最小尺寸
 * @param hWnd: 窗口句柄
 * @param minWidth: 最小宽度
 * @param minHeight: 最小高度
 */
void SetWindowMinSize(HWND hWnd, int minWidth, int minHeight) {
    if (!hWnd || !IsWindow(hWnd)) return;

    // 设置窗口最小尺寸（会触发 WM_GETMINMAXINFO）
    RECT rc = { 0, 0, minWidth, minHeight };
    AdjustWindowRectEx(&rc, GetWindowLong(hWnd, GWL_STYLE), FALSE,
        GetWindowLong(hWnd, GWL_EXSTYLE));

    // 保存最小尺寸到窗口的额外数据或使用全局变量
    // 这里我们通过 WM_GETMINMAXINFO 来处理
}

/**
 * 约束窗口的宽高比例
 * @param hWnd: 窗口句柄
 * @param width: 当前宽度
 * @param height: 当前高度
 * @param targetWidth: 目标比例宽度（如 6）
 * @param targetHeight: 目标比例高度（如 5）
 */
void ConstrainWindowAspectRatio(HWND hWnd, int width, int height,
    int targetWidth, int targetHeight) {
    if (!hWnd || !IsWindow(hWnd)) return;

    // 计算当前比例
    float currentRatio = (float)width / height;
    float targetRatio = (float)targetWidth / targetHeight;

    int newWidth = width;
    int newHeight = height;

    // 如果当前比例小于目标比例（太窄），调整宽度
    if (currentRatio < targetRatio) {
        newWidth = (int)(height * targetRatio);
        newHeight = height;
    }
    // 如果当前比例大于目标比例（太宽），调整高度
    else if (currentRatio > targetRatio) {
        newWidth = width;
        newHeight = (int)(width / targetRatio);
    }

    // 应用新尺寸
    if (newWidth != width || newHeight != height) {
        SetWindowPos(hWnd, NULL, 0, 0, newWidth, newHeight,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}
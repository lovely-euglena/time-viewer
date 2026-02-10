// fullscreen.cpp
// 全屏切换功能模块实现文件
// 提供F11快捷键和标题栏双击切换全屏功能，支持任务栏保留的非独占全屏模式

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
bool ToggleFullscreen(HWND hWnd)
{
    if (!hWnd || !IsWindow(hWnd))
        return false;

    RECT rcWorkArea;
    MONITORINFO mi = { sizeof(mi) };

    // 获取窗口所在的显示器工作区域（排除任务栏）
    HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    if (hMonitor && GetMonitorInfo(hMonitor, &mi))
    {
        rcWorkArea = mi.rcWork;
    }
    else
    {
        // 失败时回退到整个屏幕区域
        rcWorkArea.left = 0;
        rcWorkArea.top = 0;
        rcWorkArea.right = GetSystemMetrics(SM_CXSCREEN);
        rcWorkArea.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    if (!g_bFullscreen)
    {
        // 进入全屏模式
        // 保存当前窗口状态
        g_wpPrev.length = sizeof(g_wpPrev);
        if (!GetWindowPlacement(hWnd, &g_wpPrev))
            return false;

        // 保存原始窗口样式
        g_lStyle = GetWindowLong(hWnd, GWL_STYLE);
        g_lExStyle = GetWindowLong(hWnd, GWL_EXSTYLE);

        // 移除窗口边框和标题栏样式，但仍保持可调整大小能力
        LONG newStyle = g_lStyle & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
        SetWindowLong(hWnd, GWL_STYLE, newStyle);

        // 调整窗口大小至工作区域
        SetWindowPos(hWnd, HWND_TOP,
            rcWorkArea.left, rcWorkArea.top,
            rcWorkArea.right - rcWorkArea.left,
            rcWorkArea.bottom - rcWorkArea.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);

        g_bFullscreen = true;
    }
    else
    {
        // 恢复窗口化模式
        // 恢复原始窗口样式
        SetWindowLong(hWnd, GWL_STYLE, g_lStyle);
        SetWindowLong(hWnd, GWL_EXSTYLE, g_lExStyle);

        // 恢复原始窗口位置和大小
        SetWindowPlacement(hWnd, &g_wpPrev);
        SetWindowPos(hWnd, NULL, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
            SWP_FRAMECHANGED | SWP_DRAWFRAME);

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
 * 在全屏状态下防止窗口被最小化到异常尺寸
 * @param lParam: MINMAXINFO结构指针
 */
void HandleGetMinMaxInfo(LPARAM lParam)
{
    LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;
    if (g_bFullscreen)
    {
        // 设置最小跟踪大小，避免窗口过小
        lpMMI->ptMinTrackSize.x = 200;
        lpMMI->ptMinTrackSize.y = 150;
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
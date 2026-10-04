// app_state.h
// UI 状态持久化 + 崩溃恢复簿记
// 用于程序崩溃后自动拉起时复原 UI 状态（窗口位置/大小、全屏、置顶、按钮显隐）。
//
// 状态文件位置：%LOCALAPPDATA%\TimeDisplay\state.ini（不可写时回退临时目录）。
// 写入采用「临时文件 + MoveFileEx 原子替换」，避免崩溃时写到一半损坏。

#pragma once

#include <windows.h>
#include <stddef.h>

struct AppState {
    // ---- UI 状态 ----
    int  left   = 0;                 // 普通（还原）位置，工作区坐标
    int  top    = 0;
    int  right  = 0;
    int  bottom = 0;
    int  showCmd = SW_SHOWNORMAL;    // SW_SHOWNORMAL / SW_SHOWMAXIMIZED
    bool fullscreen = false;
    bool topmost = false;
    bool buttonsVisible = true;

    // ---- 崩溃簿记 ----
    int  crashCount = 0;             // 连续未正常退出的次数
    bool cleanExit = true;           // 上次是否正常退出
};

// 读取状态文件。返回 true 表示文件存在（已按文件内容填充 s，缺失字段保留当前值）。
bool AppStateLoad(AppState& s);

// 原子写入状态文件。返回是否成功。
bool AppStateSave(const AppState& s);

// 输出状态文件完整路径（buf 至少 MAX_PATH 大小）。
void AppStateGetPath(wchar_t* buf, size_t cch);

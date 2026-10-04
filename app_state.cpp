// app_state.cpp
// UI 状态持久化 + 崩溃恢复簿记实现

#include "app_state.h"

#include <strsafe.h>
#include <cstdio>

namespace {

    // 取状态目录：%LOCALAPPDATA%\TimeDisplay；不可用时回退 %TEMP%\TimeDisplay
    void GetStateDir(wchar_t* dir, size_t cch) {
        if (!dir || cch == 0) return;

        wchar_t base[MAX_PATH] = {};
        DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) {
            DWORD tn = GetTempPathW(MAX_PATH, base);
            if (tn == 0 || tn >= MAX_PATH) {
                base[0] = L'.';
                base[1] = L'\0';
            }
        }

        StringCchPrintfW(dir, cch, L"%s\\TimeDisplay", base);
    }

    void EnsureDirectory(const wchar_t* dir) {
        if (!dir || !dir[0]) return;
        DWORD attr = GetFileAttributesW(dir);
        if (attr == INVALID_FILE_ATTRIBUTES) {
            CreateDirectoryW(dir, nullptr);
        }
    }

    int BoolToInt(bool v) { return v ? 1 : 0; }

} // anonymous namespace

void AppStateGetPath(wchar_t* buf, size_t cch) {
    if (!buf || cch == 0) return;
    wchar_t dir[MAX_PATH] = {};
    GetStateDir(dir, MAX_PATH);
    EnsureDirectory(dir);
    StringCchPrintfW(buf, cch, L"%s\\state.ini", dir);
}

bool AppStateLoad(AppState& s) {
    wchar_t path[MAX_PATH] = {};
    AppStateGetPath(path, MAX_PATH);

    DWORD attr = GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        return false; // 首次运行 / 文件不存在
    }

    s.left   = (int)GetPrivateProfileIntW(L"ui", L"left",   (UINT)s.left,   path);
    s.top    = (int)GetPrivateProfileIntW(L"ui", L"top",    (UINT)s.top,    path);
    s.right  = (int)GetPrivateProfileIntW(L"ui", L"right",  (UINT)s.right,  path);
    s.bottom = (int)GetPrivateProfileIntW(L"ui", L"bottom", (UINT)s.bottom, path);
    s.showCmd = (int)GetPrivateProfileIntW(L"ui", L"showCmd", (UINT)s.showCmd, path);
    s.fullscreen     = GetPrivateProfileIntW(L"ui", L"fullscreen", BoolToInt(s.fullscreen), path) != 0;
    s.topmost        = GetPrivateProfileIntW(L"ui", L"topmost",    BoolToInt(s.topmost),    path) != 0;
    s.buttonsVisible = GetPrivateProfileIntW(L"ui", L"buttons",    BoolToInt(s.buttonsVisible), path) != 0;

    s.crashCount = (int)GetPrivateProfileIntW(L"state", L"crashCount", (UINT)s.crashCount, path);
    s.cleanExit  = GetPrivateProfileIntW(L"state", L"cleanExit", BoolToInt(s.cleanExit), path) != 0;

    return true;
}

bool AppStateSave(const AppState& s) {
    wchar_t path[MAX_PATH] = {};
    wchar_t tmp[MAX_PATH] = {};
    AppStateGetPath(path, MAX_PATH);
    StringCchPrintfW(tmp, MAX_PATH, L"%s.tmp", path);

    wchar_t content[1024] = {};
    HRESULT hr = StringCchPrintfW(content, sizeof(content) / sizeof(content[0]),
        L"[ui]\r\n"
        L"left=%d\r\n"
        L"top=%d\r\n"
        L"right=%d\r\n"
        L"bottom=%d\r\n"
        L"showCmd=%d\r\n"
        L"fullscreen=%d\r\n"
        L"topmost=%d\r\n"
        L"buttons=%d\r\n"
        L"[state]\r\n"
        L"crashCount=%d\r\n"
        L"cleanExit=%d\r\n",
        s.left, s.top, s.right, s.bottom, s.showCmd,
        BoolToInt(s.fullscreen), BoolToInt(s.topmost), BoolToInt(s.buttonsVisible),
        s.crashCount, BoolToInt(s.cleanExit));
    if (FAILED(hr)) return false;

    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    int len = lstrlenW(content);
    DWORD written = 0;
    BOOL ok = WriteFile(h, content, (DWORD)(len * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(h);
    if (!ok || written != (DWORD)(len * sizeof(wchar_t))) {
        DeleteFileW(tmp);
        return false;
    }

    // 原子替换（同目录内 rename）
    if (!MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp);
        return false;
    }
    return true;
}

// debug_log.cpp
// 调试日志实现：--debug 启用，写入文件（默认 exe 同目录 TimeDisplay.log）。
// 文件为 UTF-8（新建时写 BOM），每行带本地时间戳，多线程安全。

#include "debug_log.h"

#include <strsafe.h>
#include <cstdarg>
#include <mutex>
#include <string>

namespace {

    std::mutex g_logMutex;      // 保护 g_logPath / g_logEnabled / 文件写入
    std::wstring g_logPath;     // 日志文件完整路径
    bool g_logEnabled = false;

    // 取 exe 所在目录
    std::wstring GetExeDir() {
        wchar_t path[MAX_PATH] = {};
        DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return std::wstring();
        std::wstring full(path, n);
        size_t pos = full.find_last_of(L"\\/");
        return pos == std::wstring::npos ? std::wstring() : full.substr(0, pos);
    }

    std::wstring GetDefaultLogPath() {
        std::wstring dir = GetExeDir();
        return dir.empty() ? std::wstring(L"TimeDisplay.log") : dir + L"\\TimeDisplay.log";
    }

    // 尝试创建/打开日志文件（追加写）
    bool EnsureWritable(const std::wstring& path) {
        HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        CloseHandle(h);
        return true;
    }

    // 写入一行：时间戳 + 级别 + 内容（调用方需持有 g_logMutex）
    void WriteLine(const wchar_t* level, const wchar_t* text) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t line[1200];
        StringCchPrintfW(line, _countof(line),
            L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] %s %s\r\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
            st.wMilliseconds, level, text);

        char utf8[4096];
        int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8), nullptr, nullptr);
        if (n <= 1) return;

        HANDLE h = CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;

        DWORD written = 0;
        LARGE_INTEGER size = {};
        if (GetFileSizeEx(h, &size) && size.QuadPart == 0) {
            const char bom[3] = { (char)0xEF, (char)0xBB, (char)0xBF };
            WriteFile(h, bom, (DWORD)sizeof(bom), &written, nullptr);
        }
        WriteFile(h, utf8, (DWORD)(n - 1), &written, nullptr);
        CloseHandle(h);
    }

    // 操作系统版本（RtlGetVersion 取真实版本，不受清单兼容性声明影响）
    struct OsVersionInfo {
        ULONG dwOSVersionInfoSize;
        ULONG dwMajorVersion;
        ULONG dwMinorVersion;
        ULONG dwBuildNumber;
        ULONG dwPlatformId;
        WCHAR szCSDVersion[128];
    };
    typedef LONG(WINAPI* RtlGetVersionFn)(OsVersionInfo*);

    std::wstring GetOsVersionString() {
        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        if (hNtdll) {
            auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(hNtdll, "RtlGetVersion"));
            if (fn) {
                OsVersionInfo vi = {};
                vi.dwOSVersionInfoSize = sizeof(vi);
                if (fn(&vi) == 0) {
                    wchar_t buf[128];
                    StringCchPrintfW(buf, _countof(buf), L"Windows %lu.%lu (Build %lu)",
                        vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber);
                    return buf;
                }
            }
        }
        return L"未知版本";
    }

} // anonymous namespace

bool DebugLogEnable(const wchar_t* path) {
    std::wstring p = (path && *path) ? path : GetDefaultLogPath();

    // 传入目录（或尾部带斜杠）时，使用目录下的默认文件名
    if (path && *path) {
        DWORD attr = GetFileAttributesW(p.c_str());
        bool isDir = attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (isDir || (!p.empty() && (p.back() == L'\\' || p.back() == L'/'))) {
            if (!p.empty() && p.back() != L'\\' && p.back() != L'/') p += L'\\';
            p += L"TimeDisplay.log";
        }
    }

    // exe 目录可能不可写（如 Program Files），回退到临时目录
    if (!EnsureWritable(p)) {
        wchar_t tmp[MAX_PATH] = {};
        DWORD n = GetTempPathW(MAX_PATH, tmp);
        if (n > 0 && n < MAX_PATH) {
            std::wstring alt = std::wstring(tmp) + L"TimeDisplay.log";
            if (EnsureWritable(alt)) p = alt;
        }
    }
    if (!EnsureWritable(p)) return false;

    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        g_logPath = p;
        g_logEnabled = true;
    }

    LOGI(L"调试日志已启用: %s", p.c_str());
    LOGI(L"操作系统: %s", GetOsVersionString().c_str());
    return true;
}

void DebugLogRaw(const wchar_t* msg) {
    if (!msg || !*msg) return;
    OutputDebugStringW(msg);
    if (!g_logEnabled) return;

    std::wstring text(msg);
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r')) {
        text.pop_back();
    }
    if (text.empty()) return;

    std::lock_guard<std::mutex> lock(g_logMutex);
    WriteLine(L"I", text.c_str());
}

void DebugLog(const wchar_t* level, const wchar_t* fmt, ...) {
    if (!fmt) return;

    wchar_t buf[1024];
    va_list args;
    va_start(args, fmt);
    StringCchVPrintfW(buf, _countof(buf), fmt, args);
    va_end(args);

    OutputDebugStringW(buf);
    if (!g_logEnabled) return;

    std::lock_guard<std::mutex> lock(g_logMutex);
    WriteLine(level ? level : L"I", buf);
}

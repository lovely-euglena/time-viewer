#pragma once
#include <windows.h>

// ===== 调试日志（--debug）=====
// 日志写入文件，同时输出到 OutputDebugString（调试器可见）。
// 未启用 --debug 时仅输出到 OutputDebugString（与原有调试输出行为一致）。
//
// 级别约定（Release 构建用条件编译精简输出）：
//   LOGI —— 关键日志：Debug/Release 均编译，启用 --debug 后写入文件；
//   LOGV —— 详细日志：Release 下编译为空，仅 Debug 构建输出。
#define LOGI(...) DebugLog(L"I", __VA_ARGS__)
#ifdef _DEBUG
#define LOGV(...) DebugLog(L"V", __VA_ARGS__)
#else
#define LOGV(...) ((void)0)
#endif

// 启用调试日志。
// path 为空：默认 exe 同目录 TimeDisplay.log；
// path 为目录（或尾部带斜杠）：使用该目录下的 TimeDisplay.log；
// 指定路径不可写时自动回退到临时目录。
// 返回是否成功创建日志文件。
bool DebugLogEnable(const wchar_t* path);

// 原样记录一条消息（自动去除尾部换行，不做格式化）
void DebugLogRaw(const wchar_t* msg);

// 格式化记录一条消息（level 为单字符级别标记）
void DebugLog(const wchar_t* level, const wchar_t* fmt, ...);

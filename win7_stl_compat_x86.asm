; ===== Windows 7 兼容：x86 版 STL GetSystemTimePreciseAsFileTime 导入垫片 =====
; 说明见 main.cpp。x86 的 dllimport 导入符号带 "@4" 修饰，纯 C++ 无法定义，
; 这里为 __imp__GetSystemTimePreciseAsFileTime@4 提供本地定义，指向 C++ 回退函数
; TimeDisplay_FallbackGetSystemTimePreciseAsFileTime。
; 仅参与 Win32 构建（x64 配置在工程中将本文件排除）。
.686P
.MODEL FLAT

EXTERN _TimeDisplay_FallbackGetSystemTimePreciseAsFileTime@4:PROC

.DATA
PUBLIC __imp__GetSystemTimePreciseAsFileTime@4
__imp__GetSystemTimePreciseAsFileTime@4 DD _TimeDisplay_FallbackGetSystemTimePreciseAsFileTime@4

END

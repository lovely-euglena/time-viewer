// mingw_compat.h — MinGW-w64 cross-compilation compatibility header
// Force-include via: -include mingw_compat.h
//
// MinGW-w64's c++config.h automatically #undef min/max and sets NOMINMAX.
// Do NOT re-define min/max as macros — that breaks C++ stdlib headers.
// Instead, source files that need bare min()/max() should use:
//   using std::min; using std::max;

#ifndef MINGW_COMPAT_H
#define MINGW_COMPAT_H

// ===== DWM window corner preference (Windows 11) =====
// Old MinGW SDK may lack these constants.
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

// ===== DWM_WINDOW_CORNER_PREFERENCE enum type =====
// Must be typedef to int so #define constants work as enum values.
#ifndef DWM_WINDOW_CORNER_PREFERENCE_DEFINED
#define DWM_WINDOW_CORNER_PREFERENCE_DEFINED
#ifndef DWMWCP_DEFAULT
#define DWMWCP_DEFAULT 0
#endif
#ifndef DWMWCP_DONOTROUND
#define DWMWCP_DONOTROUND 1
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#ifndef DWMWCP_ROUNDSMALL
#define DWMWCP_ROUNDSMALL 3
#endif
typedef int DWM_WINDOW_CORNER_PREFERENCE;
#endif

#endif // MINGW_COMPAT_H

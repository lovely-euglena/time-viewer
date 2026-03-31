#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winuser.h>
#include "fullscreen.h"

// 函数声明
bool ToggleFullscreen(HWND hWnd);
bool IsFullscreen();
int GetFullscreenState();
bool HandleKeyDownForFullscreen(HWND hWnd, WPARAM wParam);
bool HandleNcLButtonDblClk(HWND hWnd, WPARAM wParam);
void HandleGetMinMaxInfo(LPARAM lParam);
void InitializeFullscreenModule(HWND hWnd);
void CleanupFullscreenModule();
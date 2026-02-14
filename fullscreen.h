#pragma once
#include <windows.h>

// 函数声明
bool ToggleFullscreen(HWND hWnd);
bool IsFullscreen();
int GetFullscreenState();
bool HandleKeyDownForFullscreen(HWND hWnd, WPARAM wParam);
bool HandleNcLButtonDblClk(HWND hWnd, WPARAM wParam);
void HandleGetMinMaxInfo(LPARAM lParam);
void InitializeFullscreenModule(HWND hWnd);
void CleanupFullscreenModule();
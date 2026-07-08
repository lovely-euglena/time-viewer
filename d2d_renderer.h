#pragma once
#include <windows.h>

struct D2DRenderInfo {
    const wchar_t* timeStr;
    const wchar_t* dateStr;
    const wchar_t* weekStr;
    const wchar_t* sourceStr;
    const wchar_t* fontFamily;
    int timeFontSize;
    int dateFontSize;
    int weekFontSize;
    int sourceFontSize;
    bool boldTime;
};

bool D2DRenderer_Init(HWND hWnd);
void D2DRenderer_Cleanup();
void D2DRenderer_OnResize(HWND hWnd, int width, int height);
bool D2DRenderer_Render(HWND hWnd, const D2DRenderInfo& info);
bool D2DRenderer_IsAvailable();

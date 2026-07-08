// d2d_renderer.cpp
// Direct2D/DirectWrite GPU 加速渲染器
// 当 D2D 初始化失败时 IsAvailable 返回 false，调用方应回退到 GDI

#include "d2d_renderer.h"

#include <d2d1.h>
#include <dwrite.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

// ===== D2D 资源（全局，单窗口单例） =====
namespace {
    // 渲染管线资源
    ID2D1Factory*          g_d2dFactory    = nullptr;
    IDWriteFactory*        g_dwriteFactory = nullptr;
    ID2D1HwndRenderTarget* g_renderTarget  = nullptr;

    // 预创建的颜色刷子
    ID2D1SolidColorBrush*  g_brushTime     = nullptr;
    ID2D1SolidColorBrush*  g_brushDate     = nullptr;
    ID2D1SolidColorBrush*  g_brushWeek     = nullptr;
    ID2D1SolidColorBrush*  g_brushSource   = nullptr;

    // 缓存的文本格式（避免每帧重复创建）
    IDWriteTextFormat*     g_fmtTime       = nullptr;
    IDWriteTextFormat*     g_fmtDate       = nullptr;
    IDWriteTextFormat*     g_fmtWeek       = nullptr;
    IDWriteTextFormat*     g_fmtSource     = nullptr;

    bool g_initialized = false;

    // 格式缓存验证参数（上一次创建时的值）
    int    g_cacheTimeSize   = 0;
    int    g_cacheDateSize   = 0;
    int    g_cacheWeekSize   = 0;
    int    g_cacheSourceSize = 0;
    float  g_cacheDpiScale   = 0.0f;
    bool   g_cacheBoldTime   = false;
    // 注意：fontFamily 由 GetActiveFontFamily() 返回，生命周期贯穿全局，无需字符串拷贝
    const wchar_t* g_cacheFamily = nullptr;

    // ---------- 辅助函数 ----------

    template <typename T>
    void SafeRelease(T*& p) {
        if (p) { p->Release(); p = nullptr; }
    }

    void InvalidateFormatCache() {
        SafeRelease(g_fmtSource);
        SafeRelease(g_fmtWeek);
        SafeRelease(g_fmtDate);
        SafeRelease(g_fmtTime);
        g_cacheTimeSize   = 0;
        g_cacheDateSize   = 0;
        g_cacheWeekSize   = 0;
        g_cacheSourceSize = 0;
        g_cacheDpiScale   = 0.0f;
        g_cacheBoldTime   = false;
        g_cacheFamily     = nullptr;
    }

    float GetWindowDpiValue(HWND hWnd) {
        float dpi = 96.0f;
        HDC hdc = GetDC(hWnd);
        if (hdc) {
            dpi = (float)GetDeviceCaps(hdc, LOGPIXELSX);
            ReleaseDC(hWnd, hdc);
        }
        return dpi;
    }

    IDWriteTextFormat* MakeFormat(
        IDWriteFactory* factory,
        const wchar_t* family,
        float dipSize,
        DWRITE_FONT_WEIGHT weight,
        DWRITE_TEXT_ALIGNMENT halign,
        DWRITE_PARAGRAPH_ALIGNMENT valign)
    {
        IDWriteTextFormat* fmt = nullptr;
        const wchar_t* f = (family && family[0]) ? family : L"Segoe UI";
        HRESULT hr = factory->CreateTextFormat(
            f, nullptr, weight,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            dipSize, L"zh-CN", &fmt);
        if (FAILED(hr)) return nullptr;
        fmt->SetTextAlignment(halign);
        fmt->SetParagraphAlignment(valign);
        return fmt;
    }

} // anonymous namespace

// ===== 公开接口 =====

bool D2DRenderer_Init(HWND hWnd) {
    if (g_initialized) return true;
    if (!hWnd || !IsWindow(hWnd)) return false;

    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        &g_d2dFactory);
    if (FAILED(hr)) return false;

    IUnknown* dwriteTemp = nullptr;
    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        &dwriteTemp);
    if (FAILED(hr)) {
        SafeRelease(g_d2dFactory);
        return false;
    }
    g_dwriteFactory = static_cast<IDWriteFactory*>(dwriteTemp);

    RECT rc;
    GetClientRect(hWnd, &rc);
    UINT w = (UINT)(rc.right - rc.left);
    UINT h = (UINT)(rc.bottom - rc.top);
    if (w == 0) w = 1;
    if (h == 0) h = 1;

    float dpi = GetWindowDpiValue(hWnd);

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_IGNORE),
        dpi, dpi);

    hr = g_d2dFactory->CreateHwndRenderTarget(
        rtProps,
        D2D1::HwndRenderTargetProperties(hWnd, D2D1::SizeU(w, h)),
        &g_renderTarget);
    if (FAILED(hr) || !g_renderTarget) {
        SafeRelease(g_dwriteFactory);
        SafeRelease(g_d2dFactory);
        return false;
    }

    // 创建颜色刷子（失败时清理返回，保持状态一致）
    auto makeBrush = [&](const D2D1::ColorF& color,
                         ID2D1SolidColorBrush*& out) -> bool {
        ID2D1SolidColorBrush* brush = nullptr;
        HRESULT hrBrush = g_renderTarget->CreateSolidColorBrush(color, &brush);
        if (FAILED(hrBrush) || !brush) return false;
        out = brush;
        return true;
    };

    if (!makeBrush(D2D1::ColorF(220 / 255.0f, 220 / 255.0f, 255 / 255.0f), g_brushTime) ||
        !makeBrush(D2D1::ColorF(200 / 255.0f, 200 / 255.0f, 240 / 255.0f), g_brushDate) ||
        !makeBrush(D2D1::ColorF(180 / 255.0f, 180 / 255.0f, 220 / 255.0f), g_brushWeek) ||
        !makeBrush(D2D1::ColorF(140 / 255.0f, 140 / 255.0f, 160 / 255.0f), g_brushSource)) {
        D2DRenderer_Cleanup();
        return false;
    }

    g_initialized = true;
    return true;
}

void D2DRenderer_Cleanup() {
    InvalidateFormatCache();
    SafeRelease(g_brushSource);
    SafeRelease(g_brushWeek);
    SafeRelease(g_brushDate);
    SafeRelease(g_brushTime);
    SafeRelease(g_renderTarget);
    SafeRelease(g_dwriteFactory);
    SafeRelease(g_d2dFactory);
    g_initialized = false;
}

void D2DRenderer_OnResize(HWND hWnd, int width, int height) {
    if (!g_renderTarget) return;

    // 尺寸变化后文本格式缓存失效（字号随窗口大小变化）
    InvalidateFormatCache();

    UINT w = (UINT)(width  > 0 ? width  : 1);
    UINT h = (UINT)(height > 0 ? height : 1);
    HRESULT hr = g_renderTarget->Resize(D2D1::SizeU(w, h));
    if (hr == D2DERR_RECREATE_TARGET) {
        D2DRenderer_Cleanup();
        D2DRenderer_Init(hWnd);
    }

    float dpi = GetWindowDpiValue(hWnd);
    if (g_renderTarget) {
        g_renderTarget->SetDpi(dpi, dpi);
    }
}

bool D2DRenderer_IsAvailable() {
    return g_initialized && g_renderTarget && g_dwriteFactory;
}

bool D2DRenderer_Render(HWND hWnd, const D2DRenderInfo& info) {
    if (!D2DRenderer_IsAvailable()) return false;

    D2D1_SIZE_F rtSize = g_renderTarget->GetSize();
    float w = rtSize.width;
    float h = rtSize.height;
    if (w <= 0 || h <= 0) return false;

    float dpi = 96.0f;
    g_renderTarget->GetDpi(&dpi, nullptr);
    float scale = dpi / 96.0f;
    auto pxToDip = [&](int px) -> float { return (float)px / scale; };

    // ---- 文本格式缓存：仅在参数变化时重建 ----
    const wchar_t* family = (info.fontFamily && info.fontFamily[0])
        ? info.fontFamily : L"Segoe UI";

    bool cacheValid = (g_fmtTime != nullptr)
        && g_cacheTimeSize   == info.timeFontSize
        && g_cacheDateSize   == info.dateFontSize
        && g_cacheWeekSize   == info.weekFontSize
        && g_cacheSourceSize == info.sourceFontSize
        && g_cacheDpiScale   == scale
        && g_cacheBoldTime   == info.boldTime
        && g_cacheFamily     == family;   // 指针比较 OK：字体名来自全局常/变量，生命周期长

    if (!cacheValid) {
        InvalidateFormatCache();

        g_fmtTime = MakeFormat(
            g_dwriteFactory, family, pxToDip(info.timeFontSize),
            info.boldTime ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        g_fmtDate = MakeFormat(
            g_dwriteFactory, family, pxToDip(info.dateFontSize),
            DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        g_fmtWeek = MakeFormat(
            g_dwriteFactory, family, pxToDip(info.weekFontSize),
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        g_fmtSource = MakeFormat(
            g_dwriteFactory, family, pxToDip(info.sourceFontSize),
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

        if (!g_fmtTime || !g_fmtDate || !g_fmtWeek || !g_fmtSource) {
            InvalidateFormatCache();
            return false;
        }

        // 缓存当前参数
        g_cacheTimeSize   = info.timeFontSize;
        g_cacheDateSize   = info.dateFontSize;
        g_cacheWeekSize   = info.weekFontSize;
        g_cacheSourceSize = info.sourceFontSize;
        g_cacheDpiScale   = scale;
        g_cacheBoldTime   = info.boldTime;
        g_cacheFamily     = family;
    }

    // ---- 渲染 ----
    g_renderTarget->BeginDraw();

    D2D1::ColorF bgColor(10.0f / 255.0f, 10.0f / 255.0f, 20.0f / 255.0f);
    g_renderTarget->Clear(bgColor);

    if (info.sourceStr && info.sourceStr[0]) {
        float srcH = (float)info.sourceFontSize + 6.0f;
        D2D1_RECT_F srcRect = D2D1::RectF(12.0f, 8.0f, w - 12.0f, 8.0f + srcH);
        g_renderTarget->DrawTextW(
            info.sourceStr, (UINT32)wcslen(info.sourceStr),
            g_fmtSource, &srcRect, g_brushSource,
            D2D1_DRAW_TEXT_OPTIONS_ENABLE_CLIP_PER_GLYPH);
    }

    float timeH  = h / 3.0f;
    float dateH  = h / 8.0f;
    float weekH  = h / 8.0f;
    float totalH = timeH + dateH + weekH + 20.0f;
    float startY = (h - totalH) / 2.0f - 20.0f;

    if (info.timeStr && info.timeStr[0]) {
        D2D1_RECT_F rect = D2D1::RectF(0.0f, startY, w, startY + timeH);
        g_renderTarget->DrawTextW(
            info.timeStr, (UINT32)wcslen(info.timeStr),
            g_fmtTime, &rect, g_brushTime,
            D2D1_DRAW_TEXT_OPTIONS_ENABLE_CLIP_PER_GLYPH);
    }

    if (info.dateStr && info.dateStr[0]) {
        float top = startY + timeH + 10.0f;
        D2D1_RECT_F rect = D2D1::RectF(0.0f, top, w, top + dateH);
        g_renderTarget->DrawTextW(
            info.dateStr, (UINT32)wcslen(info.dateStr),
            g_fmtDate, &rect, g_brushDate,
            D2D1_DRAW_TEXT_OPTIONS_ENABLE_CLIP_PER_GLYPH);
    }

    if (info.weekStr && info.weekStr[0]) {
        float top = startY + timeH + dateH + 20.0f;
        D2D1_RECT_F rect = D2D1::RectF(0.0f, top, w, top + weekH);
        g_renderTarget->DrawTextW(
            info.weekStr, (UINT32)wcslen(info.weekStr),
            g_fmtWeek, &rect, g_brushWeek,
            D2D1_DRAW_TEXT_OPTIONS_ENABLE_CLIP_PER_GLYPH);
    }

    HRESULT hrEnd = g_renderTarget->EndDraw();

    if (hrEnd == D2DERR_RECREATE_TARGET) {
        D2DRenderer_Cleanup();
        return D2DRenderer_Init(hWnd);
    }
    return SUCCEEDED(hrEnd);
}

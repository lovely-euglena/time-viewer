// gpu_renderer.cpp — Direct2D + DirectWrite GPU 加速渲染器实现

#include "gpu_renderer.h"
#include <cstdio>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

GpuRenderer::GpuRenderer()  = default;
GpuRenderer::~GpuRenderer() { Cleanup(); }

// ========== 初始化入口 ==========

bool GpuRenderer::Initialize(HWND hWnd, bool forceFallback) {
    m_hWnd = hWnd;
    m_forceFallback = forceFallback;

    if (m_forceFallback) {
        OutputDebugStringW(L"[GPU] --disable-gpu 已设置，跳过 Direct2D\n");
        return false;
    }

    if (!CreateD2DFactory())     return false;
    if (!CreateRenderTarget())   return false;
    if (!CreateDWriteFactory())  return false;

    // 标记为硬件加速（D2D 1.0 MinGW 无法直接查询渲染类型，默认硬件）
    m_hardware = true;

    // 创建共享画刷（白色，绘制时 SetColor）
    HRESULT hr = m_pRenderTarget->CreateSolidColorBrush(
        D2D1::ColorF(D2D1::ColorF::White),
        &m_pBrush
    );
    if (FAILED(hr)) {
        OutputDebugStringW(L"[GPU] 创建共享画刷失败\n");
        Cleanup();
        return false;
    }

    // 初始文本格式会在外部首次调用 SetTextFormats 时创建
    m_initialized = true;

    wchar_t buf[128];
    swprintf(buf, 128,
        L"[GPU] Direct2D 初始化成功 (%s 加速)\n",
        m_hardware ? L"硬件" : L"软件");
    OutputDebugStringW(buf);

    return true;
}

// ========== 工厂创建 ==========

bool GpuRenderer::CreateD2DFactory() {
    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        &m_pD2DFactory
    );
    if (FAILED(hr)) {
        OutputDebugStringW(L"[GPU] D2D1CreateFactory 失败，回退到 GDI\n");
        return false;
    }
    return true;
}

bool GpuRenderer::CreateRenderTarget() {
    if (!m_pD2DFactory) return false;

    RECT rc;
    GetClientRect(m_hWnd, &rc);

    D2D1_SIZE_U size = D2D1::SizeU(
        static_cast<UINT32>(rc.right - rc.left),
        static_cast<UINT32>(rc.bottom - rc.top)
    );

    // 使用 D2D1_RENDER_TARGET_TYPE_DEFAULT 让 D2D 自动选择 GPU/CPU
    HRESULT hr = m_pD2DFactory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                              D2D1_ALPHA_MODE_IGNORE),
            0, 0, D2D1_RENDER_TARGET_USAGE_NONE
        ),
        D2D1::HwndRenderTargetProperties(m_hWnd, size),
        &m_pRenderTarget
    );

    if (FAILED(hr)) {
        wchar_t buf[128];
        swprintf(buf, 128, L"[GPU] CreateHwndRenderTarget 失败 (HR=0x%08lX)，回退到 GDI\n", hr);
        OutputDebugStringW(buf);
        return false;
    }
    return true;
}

bool GpuRenderer::CreateDWriteFactory() {
    HRESULT hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(&m_pDWriteFactory)
    );
    if (FAILED(hr)) {
        OutputDebugStringW(L"[GPU] DWriteCreateFactory 失败，回退到 GDI\n");
        return false;
    }
    return true;
}

// ========== 文本格式创建/更新 ==========

void GpuRenderer::SetTextFormats(const wchar_t* fontFamily,
                                 float timeSize, float dateSize,
                                 float weekSize, float sourceSize) {
    if (!m_pDWriteFactory) return;

    // 释放旧的格式
    if (m_pTimeFormat)   { m_pTimeFormat->Release();   m_pTimeFormat   = nullptr; }
    if (m_pDateFormat)   { m_pDateFormat->Release();   m_pDateFormat   = nullptr; }
    if (m_pWeekFormat)   { m_pWeekFormat->Release();   m_pWeekFormat   = nullptr; }
    if (m_pSourceFormat) { m_pSourceFormat->Release();  m_pSourceFormat = nullptr; }

    auto createFormat = [&](float size, DWRITE_FONT_WEIGHT weight) -> IDWriteTextFormat* {
        IDWriteTextFormat* fmt = nullptr;
        HRESULT hr = m_pDWriteFactory->CreateTextFormat(
            fontFamily,
            nullptr,                      // 使用系统字体集合
            weight,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            size,
            L"zh-CN",
            &fmt
        );
        if (FAILED(hr) && fmt) { fmt->Release(); return nullptr; }

        // 设置文本对齐和段落对齐
        if (fmt) {
            fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        return fmt;
    };

    m_pTimeFormat   = createFormat(timeSize,   DWRITE_FONT_WEIGHT_BOLD);
    m_pDateFormat   = createFormat(dateSize,   DWRITE_FONT_WEIGHT_SEMI_BOLD);
    m_pWeekFormat   = createFormat(weekSize,   DWRITE_FONT_WEIGHT_SEMI_BOLD);
    m_pSourceFormat = createFormat(sourceSize, DWRITE_FONT_WEIGHT_NORMAL);

    // 来源文本左对齐（在 DrawTextTopLeft 中单独设置）
    if (m_pSourceFormat) {
        m_pSourceFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        m_pSourceFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }
}

// ========== 渲染操作 ==========

bool GpuRenderer::Resize() {
    if (!m_initialized || !m_pRenderTarget) return false;

    RECT rc;
    GetClientRect(m_hWnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(
        static_cast<UINT32>(rc.right - rc.left),
        static_cast<UINT32>(rc.bottom - rc.top)
    );

    HRESULT hr = m_pRenderTarget->Resize(size);
    if (FAILED(hr)) {
        // 渲染目标可能需要重建
        m_pRenderTarget->Release();
        m_pRenderTarget = nullptr;
        return CreateRenderTarget();
    }
    return true;
}

bool GpuRenderer::BeginDraw() {
    if (!m_initialized || !m_pRenderTarget) return false;
    m_pRenderTarget->BeginDraw();
    return true;
}

bool GpuRenderer::EndDraw() {
    if (!m_initialized || !m_pRenderTarget) return false;

    HRESULT hr = m_pRenderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        // 渲染目标失效（如显示器切换、GPU 驱动重置），需要重建
        OutputDebugStringW(L"[GPU] 渲染目标需重建\n");
        m_pRenderTarget->Release();
        m_pRenderTarget = nullptr;
        if (!CreateRenderTarget()) {
            // 重建失败 → 标记初始化失败，由外部触发 GDI 回退
            m_initialized = false;
            return false;
        }
        // 重建成功，画刷也需要重建
        if (m_pBrush) { m_pBrush->Release(); m_pBrush = nullptr; }
        if (m_pRenderTarget) {
            m_pRenderTarget->CreateSolidColorBrush(
                D2D1::ColorF(D2D1::ColorF::White), &m_pBrush);
        }
        return true;
    }

    if (FAILED(hr)) {
        wchar_t buf[128];
        swprintf(buf, 128, L"[GPU] EndDraw 失败 (HR=0x%08lX)\n", hr);
        OutputDebugStringW(buf);
        return false;
    }
    return true;
}

void GpuRenderer::ClearBackground(D2D1_COLOR_F color) {
    if (!m_initialized || !m_pRenderTarget) return;
    m_pRenderTarget->Clear(color);
}

void GpuRenderer::DrawTextCentered(const std::wstring& text,
                                   IDWriteTextFormat* format,
                                   const D2D1_RECT_F& rect,
                                   D2D1_COLOR_F color) {
    if (!m_initialized || !m_pRenderTarget || !m_pBrush || !format) return;
    m_pBrush->SetColor(color);
    m_pRenderTarget->DrawText(
        text.c_str(),
        static_cast<UINT32>(text.length()),
        format,
        rect,
        m_pBrush,
        D2D1_DRAW_TEXT_OPTIONS_NONE
    );
}

void GpuRenderer::DrawTextTopLeft(const std::wstring& text,
                                  IDWriteTextFormat* format,
                                  const D2D1_RECT_F& rect,
                                  D2D1_COLOR_F color) {
    if (!m_initialized || !m_pRenderTarget || !m_pBrush || !format) return;
    m_pBrush->SetColor(color);
    m_pRenderTarget->DrawText(
        text.c_str(),
        static_cast<UINT32>(text.length()),
        format,
        rect,
        m_pBrush,
        D2D1_DRAW_TEXT_OPTIONS_NONE
    );
}

// ========== 资源清理 ==========

void GpuRenderer::ReleaseResources() {
    if (m_pTimeFormat)   { m_pTimeFormat->Release();   m_pTimeFormat   = nullptr; }
    if (m_pDateFormat)   { m_pDateFormat->Release();   m_pDateFormat   = nullptr; }
    if (m_pWeekFormat)   { m_pWeekFormat->Release();   m_pWeekFormat   = nullptr; }
    if (m_pSourceFormat) { m_pSourceFormat->Release();  m_pSourceFormat = nullptr; }
    if (m_pBrush)        { m_pBrush->Release();         m_pBrush        = nullptr; }
    if (m_pRenderTarget) { m_pRenderTarget->Release();  m_pRenderTarget = nullptr; }
    if (m_pDWriteFactory){ m_pDWriteFactory->Release(); m_pDWriteFactory= nullptr; }
    if (m_pD2DFactory)   { m_pD2DFactory->Release();    m_pD2DFactory   = nullptr; }
}

void GpuRenderer::Cleanup() {
    ReleaseResources();
    m_initialized  = false;
    m_hardware     = false;
    m_hWnd         = nullptr;
}

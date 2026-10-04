// d2d_renderer.cpp
// Direct2D/DirectWrite 渲染器
//
// 渲染路径优先级：
//   1. Win8+：D2D1.1 ID2D1DeviceContext + DXGI flip-model 交换链
//      （垂直同步、由 DWM 合成，减少大屏撕裂/闪烁）；
//   2. 回退：ID2D1HwndRenderTarget（Win7 / 无 D2D1.1 / 分层窗口）；
//   3. 再失败：由调用方回退 GDI。
//
// 所有新 API 均运行时按需加载，保证 Win7 上仍能启动。

#include "d2d_renderer.h"
#include "debug_log.h"

#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <d3d11.h>

// ===== 动态加载 d2d1.dll / dwrite.dll / d3d11.dll（Windows 7 兼容） =====
// Win7 未安装平台更新（KB2670838）时系统没有 d2d1.dll；若静态导入，进程会在
// 启动时因缺少 DLL 直接失败，GDI 回退逻辑没有机会执行。改为运行时按名称加载：
// 加载失败时 D2DRenderer_Init 返回 false，由调用方回退 GDI 渲染。
// （按名称加载也比静态导入库的序号引用更稳，不同系统版本的导出序号可能不同）

namespace {

    // d2d1.dll / dwrite.dll / d3d11.dll 运行时函数指针
    HMODULE g_hD2D1Lib = nullptr;
    HMODULE g_hDWriteLib = nullptr;
    HMODULE g_hD3D11Lib = nullptr;

    HRESULT(WINAPI* g_pfnD2D1CreateFactory)(
        D2D1_FACTORY_TYPE, REFIID, const D2D1_FACTORY_OPTIONS*, void**) = nullptr;
    HRESULT(WINAPI* g_pfnDWriteCreateFactory)(
        DWRITE_FACTORY_TYPE, REFIID, IUnknown**) = nullptr;

    typedef HRESULT(WINAPI* PFN_D3D11CreateDevice)(
        IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
        const D3D_FEATURE_LEVEL*, UINT, UINT,
        ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
    PFN_D3D11CreateDevice g_pfnD3D11CreateDevice = nullptr;

    bool LoadRenderLibraries() {
        if (!g_hD2D1Lib) {
            g_hD2D1Lib = LoadLibraryW(L"d2d1.dll");
        }
        if (g_hD2D1Lib && !g_pfnD2D1CreateFactory) {
            g_pfnD2D1CreateFactory = reinterpret_cast<decltype(g_pfnD2D1CreateFactory)>(
                GetProcAddress(g_hD2D1Lib, "D2D1CreateFactory"));
        }
        if (!g_hDWriteLib) {
            g_hDWriteLib = LoadLibraryW(L"dwrite.dll");
        }
        if (g_hDWriteLib && !g_pfnDWriteCreateFactory) {
            g_pfnDWriteCreateFactory = reinterpret_cast<decltype(g_pfnDWriteCreateFactory)>(
                GetProcAddress(g_hDWriteLib, "DWriteCreateFactory"));
        }
        // 记录缺失原因，便于在 Win7 等旧系统上定位回退原因
        if (!g_pfnD2D1CreateFactory) {
            LOGI(g_hD2D1Lib ? L"D2D: d2d1.dll 缺少 D2D1CreateFactory 导出"
                            : L"D2D: d2d1.dll 加载失败（系统未安装或版本过低）");
        }
        if (!g_pfnDWriteCreateFactory) {
            LOGI(g_hDWriteLib ? L"D2D: dwrite.dll 缺少 DWriteCreateFactory 导出"
                              : L"D2D: dwrite.dll 加载失败（系统未安装或版本过低）");
        }
        return g_pfnD2D1CreateFactory != nullptr && g_pfnDWriteCreateFactory != nullptr;
    }

    bool LoadD3D11Library() {
        if (!g_hD3D11Lib) {
            g_hD3D11Lib = LoadLibraryW(L"d3d11.dll");
        }
        if (g_hD3D11Lib && !g_pfnD3D11CreateDevice) {
            g_pfnD3D11CreateDevice = reinterpret_cast<PFN_D3D11CreateDevice>(
                GetProcAddress(g_hD3D11Lib, "D3D11CreateDevice"));
        }
        return g_pfnD3D11CreateDevice != nullptr;
    }

    // ===== D2D 资源（全局，单窗口单例） =====
    // 公共工厂
    ID2D1Factory*          g_d2dFactory    = nullptr;
    IDWriteFactory*        g_dwriteFactory = nullptr;

    // ---- 路径 1：D2D1.1 DeviceContext + DXGI flip-model（Win8+）----
    bool                   g_useDeviceContext = false;
    ID3D11Device*          g_d3dDevice    = nullptr;
    IDXGISwapChain1*       g_swapChain    = nullptr;
    ID2D1Device*           g_d2dDevice    = nullptr;
    ID2D1DeviceContext*    g_d2dContext   = nullptr;
    ID2D1Bitmap1*          g_backBuffer   = nullptr;

    // ---- 路径 2：ID2D1HwndRenderTarget（回退）----
    ID2D1HwndRenderTarget* g_renderTarget = nullptr;

    // 预创建的颜色刷子（从当前活动的 RenderTarget 创建）
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
    bool g_softwareMode = false;

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

    // 当前活动的 RenderTarget（两条路径的公共基类 ID2D1RenderTarget）
    ID2D1RenderTarget* GetActiveRT() {
        if (g_useDeviceContext) {
            return static_cast<ID2D1RenderTarget*>(g_d2dContext);
        }
        return static_cast<ID2D1RenderTarget*>(g_renderTarget);
    }

    void ReleaseBrushes() {
        SafeRelease(g_brushSource);
        SafeRelease(g_brushWeek);
        SafeRelease(g_brushDate);
        SafeRelease(g_brushTime);
    }

    bool CreateBrushes() {
        ID2D1RenderTarget* rt = GetActiveRT();
        if (!rt) return false;

        auto makeBrush = [&](const D2D1::ColorF& color,
                             ID2D1SolidColorBrush*& out) -> bool {
            ID2D1SolidColorBrush* brush = nullptr;
            if (FAILED(rt->CreateSolidColorBrush(color, &brush)) || !brush) return false;
            out = brush;
            return true;
        };

        return makeBrush(D2D1::ColorF(220 / 255.0f, 220 / 255.0f, 255 / 255.0f), g_brushTime)
            && makeBrush(D2D1::ColorF(200 / 255.0f, 200 / 255.0f, 240 / 255.0f), g_brushDate)
            && makeBrush(D2D1::ColorF(180 / 255.0f, 180 / 255.0f, 220 / 255.0f), g_brushWeek)
            && makeBrush(D2D1::ColorF(140 / 255.0f, 140 / 255.0f, 160 / 255.0f), g_brushSource);
    }

    // 释放所有与设备相关的资源（保留工厂与 DLL），用于设备丢失后重建
    void ReleaseDeviceResources() {
        InvalidateFormatCache();
        ReleaseBrushes();
        if (g_d2dContext) {
            g_d2dContext->SetTarget(nullptr); // 先解除对后台缓冲位图的引用
        }
        SafeRelease(g_backBuffer);
        SafeRelease(g_d2dContext);
        SafeRelease(g_d2dDevice);
        SafeRelease(g_swapChain);
        SafeRelease(g_d3dDevice);
        SafeRelease(g_renderTarget);
        g_useDeviceContext = false;
        g_initialized = false;
    }

    // 用交换链后台缓冲创建 D2D 绘制目标位图，并设为 DeviceContext 的目标
    bool CreateBackBufferTarget(HWND hWnd) {
        if (!g_swapChain || !g_d2dContext) return false;

        IDXGISurface* surface = nullptr;
        if (FAILED(g_swapChain->GetBuffer(0, __uuidof(IDXGISurface),
            reinterpret_cast<void**>(&surface))) || !surface) {
            return false;
        }

        float dpi = GetWindowDpiValue(hWnd);

        D2D1_BITMAP_PROPERTIES1 props = {};
        props.pixelFormat.format    = DXGI_FORMAT_B8G8R8A8_UNORM;
        props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
        props.dpiX = dpi;
        props.dpiY = dpi;
        props.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

        HRESULT hr = g_d2dContext->CreateBitmapFromDxgiSurface(surface, &props, &g_backBuffer);
        surface->Release();
        if (FAILED(hr) || !g_backBuffer) return false;

        g_d2dContext->SetTarget(g_backBuffer);
        g_d2dContext->SetDpi(dpi, dpi);
        return true;
    }

    // 路径 1：D2D1.1 DeviceContext + DXGI flip-model 交换链
    bool InitFlipModelPath(HWND hWnd, UINT w, UINT h, float dpi) {
        (void)dpi; // DPI 在 CreateBackBufferTarget 内重新读取

        ID2D1Factory1* factory1 = nullptr;
        if (FAILED(g_d2dFactory->QueryInterface(__uuidof(ID2D1Factory1),
            reinterpret_cast<void**>(&factory1))) || !factory1) {
            return false;
        }

        bool ok = false;
        IDXGIDevice*             dxgiDevice = nullptr;
        IDXGIAdapter*            adapter    = nullptr;
        IDXGIFactory2*           factory2   = nullptr;
        ID3D11DeviceContext*     immediate  = nullptr;

        do {
            if (!LoadD3D11Library()) {
                LOGV(L"D2D: d3d11.dll 不可用，跳过 flip-model");
                break;
            }

            D3D_FEATURE_LEVEL levels[] = {
                D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
                D3D_FEATURE_LEVEL_9_3,  D3D_FEATURE_LEVEL_9_2,  D3D_FEATURE_LEVEL_9_1
            };
            UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
            HRESULT hr = g_pfnD3D11CreateDevice(
                nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                levels, (UINT)(sizeof(levels) / sizeof(levels[0])), D3D11_SDK_VERSION,
                &g_d3dDevice, nullptr, &immediate);
            if (FAILED(hr) || !g_d3dDevice) {
                LOGV(L"D2D: D3D11CreateDevice 失败 (0x%08X)", (unsigned)hr);
                break;
            }

            if (FAILED(g_d3dDevice->QueryInterface(__uuidof(IDXGIDevice),
                reinterpret_cast<void**>(&dxgiDevice))) || !dxgiDevice) {
                break;
            }
            if (FAILED(dxgiDevice->GetAdapter(&adapter)) || !adapter) {
                break;
            }
            if (FAILED(adapter->GetParent(__uuidof(IDXGIFactory2),
                reinterpret_cast<void**>(&factory2))) || !factory2) {
                break;
            }

            DXGI_SWAP_CHAIN_DESC1 desc = {};
            desc.Width       = w;
            desc.Height      = h;
            desc.Format      = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.Stereo      = FALSE;
            desc.SampleDesc.Count   = 1;
            desc.SampleDesc.Quality = 0;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = 2;
            desc.Scaling     = DXGI_SCALING_STRETCH;
            desc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            desc.AlphaMode   = DXGI_ALPHA_MODE_IGNORE;
            desc.Flags       = 0;

            hr = factory2->CreateSwapChainForHwnd(
                g_d3dDevice, hWnd, &desc, nullptr, nullptr, &g_swapChain);
            if (FAILED(hr) || !g_swapChain) {
                LOGV(L"D2D: CreateSwapChainForHwnd 失败 (0x%08X)", (unsigned)hr);
                break;
            }
            // 全屏由程序自行管理，禁止 DXGI 自动处理 Alt+Enter
            factory2->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);

            hr = factory1->CreateDevice(dxgiDevice, &g_d2dDevice);
            if (FAILED(hr) || !g_d2dDevice) {
                LOGV(L"D2D: CreateDevice 失败 (0x%08X)", (unsigned)hr);
                break;
            }
            hr = g_d2dDevice->CreateDeviceContext(
                D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &g_d2dContext);
            if (FAILED(hr) || !g_d2dContext) {
                LOGV(L"D2D: CreateDeviceContext 失败 (0x%08X)", (unsigned)hr);
                break;
            }
            if (!CreateBackBufferTarget(hWnd)) {
                LOGV(L"D2D: 创建后台缓冲目标失败");
                break;
            }

            g_useDeviceContext = true;
            ok = true;
        } while (false);

        if (immediate) immediate->Release();
        if (factory2)  factory2->Release();
        if (adapter)   adapter->Release();
        if (dxgiDevice) dxgiDevice->Release();
        factory1->Release();

        if (!ok) {
            // 清理半成品，交由调用方回退 HWND RT
            ReleaseDeviceResources();
        }
        return ok;
    }

    // 路径 2：ID2D1HwndRenderTarget（Win7 / 回退）
    bool InitHwndRenderTargetPath(HWND hWnd, UINT w, UINT h, float dpi) {
        D2D1_RENDER_TARGET_TYPE rtType = D2D1_RENDER_TARGET_TYPE_DEFAULT;
        if (g_softwareMode) {
            rtType = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
        }

        D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
            rtType,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            dpi, dpi);

        HRESULT hr = g_d2dFactory->CreateHwndRenderTarget(
            rtProps,
            D2D1::HwndRenderTargetProperties(hWnd, D2D1::SizeU(w, h)),
            &g_renderTarget);

        // 硬件加速失败 → 自动回退到软件渲染
        if ((FAILED(hr) || !g_renderTarget) && !g_softwareMode) {
            HRESULT hwHr = hr;
            rtProps.type = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
            hr = g_d2dFactory->CreateHwndRenderTarget(
                rtProps,
                D2D1::HwndRenderTargetProperties(hWnd, D2D1::SizeU(w, h)),
                &g_renderTarget);
            if (SUCCEEDED(hr) && g_renderTarget) {
                g_softwareMode = true;
                LOGI(L"D2D: 硬件渲染不可用 (HRESULT=0x%08X)，已回退软件渲染", (unsigned)hwHr);
            }
        }

        if (FAILED(hr) || !g_renderTarget) {
            LOGI(L"D2D: 渲染目标创建失败 (HRESULT=0x%08X)", (unsigned)hr);
            return false;
        }
        return true;
    }

    bool RenderInternal(HWND hWnd, const D2DRenderInfo& info, bool allowRetry);

} // anonymous namespace

// ===== 公开接口 =====

void D2DRenderer_ForceSoftwareRendering(bool enable) {
    g_softwareMode = enable;
}

bool D2DRenderer_Init(HWND hWnd) {
    if (g_initialized) return true;
    if (!hWnd || !IsWindow(hWnd)) return false;

    // Win7 无平台更新时 d2d1.dll/dwrite.dll 可能不存在，直接失败由调用方回退 GDI
    if (!LoadRenderLibraries()) return false;

    if (!g_d2dFactory) {
        HRESULT hr = g_pfnD2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED,
            __uuidof(ID2D1Factory),
            nullptr,
            (void**)&g_d2dFactory);
        if (FAILED(hr) || !g_d2dFactory) {
            LOGI(L"D2D: D2D1CreateFactory 失败 (HRESULT=0x%08X)", (unsigned)hr);
            return false;
        }
    }

    if (!g_dwriteFactory) {
        IUnknown* dwriteTemp = nullptr;
        HRESULT hr = g_pfnDWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            &dwriteTemp);
        if (FAILED(hr) || !dwriteTemp) {
            LOGI(L"D2D: DWriteCreateFactory 失败 (HRESULT=0x%08X)", (unsigned)hr);
            SafeRelease(g_d2dFactory);
            return false;
        }
        g_dwriteFactory = static_cast<IDWriteFactory*>(dwriteTemp);
    }

    RECT rc;
    GetClientRect(hWnd, &rc);
    UINT w = (UINT)(rc.right - rc.left);
    UINT h = (UINT)(rc.bottom - rc.top);
    if (w == 0) w = 1;
    if (h == 0) h = 1;

    float dpi = GetWindowDpiValue(hWnd);

    // 分层窗口（WS_EX_LAYERED）与 flip-model 交换链不兼容，直接走 HWND RT
    bool layered = (GetWindowLongW(hWnd, GWL_EXSTYLE) & WS_EX_LAYERED) != 0;

    bool inited = false;

    if (!g_softwareMode && !layered) {
        if (InitFlipModelPath(hWnd, w, h, dpi)) {
            inited = CreateBrushes();
            if (inited) {
                LOGI(L"D2D: 初始化成功 (flip-model 硬件加速)");
            } else {
                LOGI(L"D2D: 颜色刷创建失败 (flip-model)");
                ReleaseDeviceResources();
            }
        }
    } else if (layered) {
        LOGV(L"D2D: 分层窗口，跳过 flip-model，使用 ID2D1HwndRenderTarget");
    }

    if (!inited) {
        if (InitHwndRenderTargetPath(hWnd, w, h, dpi)) {
            inited = CreateBrushes();
            if (!inited) {
                LOGI(L"D2D: 颜色刷创建失败");
                D2DRenderer_Cleanup();
                return false;
            }
            LOGI(L"D2D: 初始化成功 (%s)", g_softwareMode ? L"软件渲染" : L"硬件加速(HWND RT)");
        } else {
            SafeRelease(g_dwriteFactory);
            SafeRelease(g_d2dFactory);
            return false;
        }
    }

    g_initialized = true;
    return true;
}

void D2DRenderer_Cleanup() {
    ReleaseDeviceResources();
    SafeRelease(g_dwriteFactory);
    SafeRelease(g_d2dFactory);
}

bool D2DRenderer_IsAvailable() {
    if (!g_initialized || !g_dwriteFactory) return false;
    return GetActiveRT() != nullptr;
}

void D2DRenderer_OnResize(HWND hWnd, int width, int height) {
    if (!g_initialized) return;

    // 尺寸变化后文本格式缓存失效（字号随窗口大小变化）
    InvalidateFormatCache();

    UINT w = (UINT)(width  > 0 ? width  : 1);
    UINT h = (UINT)(height > 0 ? height : 1);

    if (g_useDeviceContext) {
        if (!g_swapChain || !g_d2dContext) {
            ReleaseDeviceResources();
            D2DRenderer_Init(hWnd);
            return;
        }
        // ResizeBuffers 前必须解除目标并释放后台缓冲位图，否则会因仍有引用而失败
        if (g_d2dContext) g_d2dContext->SetTarget(nullptr);
        SafeRelease(g_backBuffer);

        HRESULT hr = g_swapChain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr)) {
            LOGV(L"D2D: ResizeBuffers 失败 (0x%08X)，重建设备", (unsigned)hr);
            ReleaseDeviceResources();
            D2DRenderer_Init(hWnd);
            return;
        }
        if (!CreateBackBufferTarget(hWnd)) {
            LOGV(L"D2D: 重建后台缓冲失败，重建设备");
            ReleaseDeviceResources();
            D2DRenderer_Init(hWnd);
        }
        return;
    }

    if (!g_renderTarget) return;

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

namespace {

    bool RenderInternal(HWND hWnd, const D2DRenderInfo& info, bool allowRetry) {
        if (!D2DRenderer_IsAvailable()) return false;

        ID2D1RenderTarget* rt = GetActiveRT();
        if (!rt) return false;

        D2D1_SIZE_F rtSize = rt->GetSize();
        float w = rtSize.width;
        float h = rtSize.height;
        if (w <= 0 || h <= 0) return false;

        float dpi = GetWindowDpiValue(hWnd);
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

            g_cacheTimeSize   = info.timeFontSize;
            g_cacheDateSize   = info.dateFontSize;
            g_cacheWeekSize   = info.weekFontSize;
            g_cacheSourceSize = info.sourceFontSize;
            g_cacheDpiScale   = scale;
            g_cacheBoldTime   = info.boldTime;
            g_cacheFamily     = family;
        }

        // ---- 渲染 ----
        rt->BeginDraw();

        D2D1::ColorF bgColor(10.0f / 255.0f, 10.0f / 255.0f, 20.0f / 255.0f);
        rt->Clear(bgColor);

        if (info.sourceStr && info.sourceStr[0]) {
            float srcH = (float)info.sourceFontSize + 6.0f;
            D2D1_RECT_F srcRect = D2D1::RectF(12.0f, 8.0f, w - 12.0f, 8.0f + srcH);
            rt->DrawTextW(
                info.sourceStr, (UINT32)wcslen(info.sourceStr),
                g_fmtSource, &srcRect, g_brushSource,
                D2D1_DRAW_TEXT_OPTIONS_NONE);
        }

        float timeH  = h / 3.0f;
        float dateH  = h / 8.0f;
        float weekH  = h / 8.0f;
        float totalH = timeH + dateH + weekH + 20.0f;
        float startY = (h - totalH) / 2.0f - 20.0f;

        if (info.timeStr && info.timeStr[0]) {
            D2D1_RECT_F rect = D2D1::RectF(0.0f, startY, w, startY + timeH);
            rt->DrawTextW(
                info.timeStr, (UINT32)wcslen(info.timeStr),
                g_fmtTime, &rect, g_brushTime,
                D2D1_DRAW_TEXT_OPTIONS_NONE);
        }

        if (info.dateStr && info.dateStr[0]) {
            float top = startY + timeH + 10.0f;
            D2D1_RECT_F rect = D2D1::RectF(0.0f, top, w, top + dateH);
            rt->DrawTextW(
                info.dateStr, (UINT32)wcslen(info.dateStr),
                g_fmtDate, &rect, g_brushDate,
                D2D1_DRAW_TEXT_OPTIONS_NONE);
        }

        if (info.weekStr && info.weekStr[0]) {
            float top = startY + timeH + dateH + 20.0f;
            D2D1_RECT_F rect = D2D1::RectF(0.0f, top, w, top + weekH);
            rt->DrawTextW(
                info.weekStr, (UINT32)wcslen(info.weekStr),
                g_fmtWeek, &rect, g_brushWeek,
                D2D1_DRAW_TEXT_OPTIONS_NONE);
        }

        HRESULT hrEnd = rt->EndDraw();

        if (FAILED(hrEnd)) {
            if (hrEnd == D2DERR_RECREATE_TARGET && allowRetry) {
                LOGV(L"D2D: EndDraw 请求重建目标，重建后重绘");
                ReleaseDeviceResources();
                if (D2DRenderer_Init(hWnd)) {
                    return RenderInternal(hWnd, info, false);
                }
            }
            return false;
        }

        // flip-model 路径：提交并垂直同步
        if (g_useDeviceContext && g_swapChain) {
            HRESULT hrPresent = g_swapChain->Present(1, 0);
            if (FAILED(hrPresent)) {
                bool deviceLost = (hrPresent == DXGI_ERROR_DEVICE_REMOVED ||
                                   hrPresent == DXGI_ERROR_DEVICE_RESET);
                LOGV(L"D2D: Present 失败 (0x%08X)", (unsigned)hrPresent);
                if (deviceLost && allowRetry) {
                    ReleaseDeviceResources();
                    if (D2DRenderer_Init(hWnd)) {
                        return RenderInternal(hWnd, info, false);
                    }
                }
                return false;
            }
        }
        return true;
    }

} // anonymous namespace

bool D2DRenderer_Render(HWND hWnd, const D2DRenderInfo& info) {
    return RenderInternal(hWnd, info, true);
}

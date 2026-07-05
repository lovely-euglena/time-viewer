#pragma once
// gpu_renderer.h — Direct2D + DirectWrite GPU 加速渲染器
// 当 GPU 初始化失败或 --disable-gpu 时，自动回退到 GDI 渲染路径

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>

class GpuRenderer {
public:
    GpuRenderer();
    ~GpuRenderer();

    // 初始化 Direct2D 工厂 + DirectWrite 工厂 + HWND 渲染目标
    // hWnd:     关联窗口句柄
    // forceFallback: 为 true 时直接跳过 Direct2D（等价于 --disable-gpu）
    // 返回 true 表示 GPU 渲染器就绪
    bool Initialize(HWND hWnd, bool forceFallback = false);

    // 窗口大小/DPI 变化时重建渲染目标
    bool Resize();

    // 开始渲染帧（在 WM_PAINT 中 BeginPaint 之后调用）
    bool BeginDraw();

    // 结束渲染并提交到屏幕
    bool EndDraw();

    // 清屏绘制纯色背景
    void ClearBackground(D2D1_COLOR_F color);

    // 居中绘制文本（时间/日期/星期使用）
    void DrawTextCentered(const std::wstring& text,
                          IDWriteTextFormat* format,
                          const D2D1_RECT_F& rect,
                          D2D1_COLOR_F color);

    // 左上角绘制文本（时间来源标签使用）
    void DrawTextTopLeft(const std::wstring& text,
                         IDWriteTextFormat* format,
                         const D2D1_RECT_F& rect,
                         D2D1_COLOR_F color);

    // 创建/更新 DirectWrite 文本格式（在字体或 DPI 变化时调用）
    void SetTextFormats(const wchar_t* fontFamily,
                        float timeSize, float dateSize,
                        float weekSize, float sourceSize);

    // 获取文本格式（外部用于创建自定义格式场合）
    IDWriteTextFormat* GetTimeFormat()   const { return m_pTimeFormat; }
    IDWriteTextFormat* GetDateFormat()   const { return m_pDateFormat; }
    IDWriteTextFormat* GetWeekFormat()   const { return m_pWeekFormat; }
    IDWriteTextFormat* GetSourceFormat() const { return m_pSourceFormat; }

    // 查询状态
    bool IsInitialized()          const { return m_initialized; }
    bool IsHardwareAccelerated()  const { return m_hardware; }

    // 清理所有 COM 资源
    void Cleanup();

private:
    bool CreateD2DFactory();
    bool CreateRenderTarget();
    bool CreateDWriteFactory();
    void ReleaseResources();

    HWND     m_hWnd = nullptr;
    bool     m_initialized = false;
    bool     m_hardware = false;
    bool     m_forceFallback = false;

    ID2D1Factory*         m_pD2DFactory     = nullptr;
    ID2D1HwndRenderTarget*m_pRenderTarget   = nullptr;
    IDWriteFactory*       m_pDWriteFactory  = nullptr;

    // 共享画刷——绘制前 SetColor，避免每帧创建销毁
    ID2D1SolidColorBrush* m_pBrush = nullptr;

    IDWriteTextFormat*    m_pTimeFormat     = nullptr;
    IDWriteTextFormat*    m_pDateFormat     = nullptr;
    IDWriteTextFormat*    m_pWeekFormat     = nullptr;
    IDWriteTextFormat*    m_pSourceFormat   = nullptr;
};

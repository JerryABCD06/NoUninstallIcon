// Direct3D11 + Direct2D + DirectComposition surface used by the main window,
// plus an off-screen renderer for deterministic screenshots.
#pragma once

#include "common.h"

struct Gfx
{
    ComPtr<ID3D11Device> d3d;
    ComPtr<ID3D11DeviceContext> d3dContext;
    ComPtr<IDXGISwapChain1> swapChain;
    ComPtr<IDCompositionDevice> composition;
    ComPtr<IDCompositionTarget> compositionTarget;
    ComPtr<IDCompositionVisual> compositionVisual;
    ComPtr<ID2D1Factory1> d2dFactory;
    ComPtr<ID2D1Device> d2dDevice;
    ComPtr<ID2D1DeviceContext> dc;
    ComPtr<ID2D1Bitmap1> backBuffer;

    UINT pixelWidth = 0;
    UINT pixelHeight = 0;
    float dpi = 96.0f;
};

bool GfxCreate(Gfx& gfx, HWND hwnd, UINT width, UINT height, float dpi);
bool GfxResize(Gfx& gfx, UINT width, UINT height, float dpi);
void GfxBegin(Gfx& gfx);
void GfxEnd(Gfx& gfx);
void GfxDestroy(Gfx& gfx);

bool RenderToPng(ID2D1Factory* factory, const std::wstring& path, UINT width, UINT height,
                 D2D1_COLOR_F background, const std::function<void(ID2D1RenderTarget*)>& draw);

// Grabs the on-screen pixels of a window (used to verify the real window chrome).
bool CaptureWindowToPng(HWND hwnd, const std::wstring& path);

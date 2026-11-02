#include "gfx.h"

#include "util.h"

namespace {

bool CreateDeviceAndSwapChain(Gfx& gfx, UINT width, UINT height)
{
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 3,
                                   D3D11_SDK_VERSION, gfx.d3d.GetAddressOf(), nullptr,
                                   gfx.d3dContext.GetAddressOf());
    if (FAILED(hr))
    {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, 3,
                               D3D11_SDK_VERSION, gfx.d3d.GetAddressOf(), nullptr,
                               gfx.d3dContext.GetAddressOf());
    }
    if (FAILED(hr)) return false;

    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(gfx.d3d.As(&dxgiDevice))) return false;

    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(adapter.GetAddressOf()))) return false;

    ComPtr<IDXGIFactory2> factory;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf())))) return false;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    if (FAILED(factory->CreateSwapChainForComposition(gfx.d3d.Get(), &desc, nullptr,
                                                      gfx.swapChain.GetAddressOf())))
        return false;

    D2D1_FACTORY_OPTIONS options{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options,
                                 reinterpret_cast<void**>(gfx.d2dFactory.GetAddressOf()))))
        return false;

    if (FAILED(gfx.d2dFactory->CreateDevice(dxgiDevice.Get(), gfx.d2dDevice.GetAddressOf()))) return false;
    if (FAILED(gfx.d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                                  gfx.dc.GetAddressOf())))
        return false;

    if (FAILED(DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(gfx.composition.GetAddressOf()))))
        return false;
    return true;
}

bool CreateBackBuffer(Gfx& gfx, UINT width, UINT height, float dpi)
{
    gfx.dc->SetTarget(nullptr);
    gfx.backBuffer.Reset();
    gfx.dc->SetDpi(dpi, dpi);

    ComPtr<IDXGISurface> surface;
    if (FAILED(gfx.swapChain->GetBuffer(0, IID_PPV_ARGS(surface.GetAddressOf())))) return false;

    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi);
    if (FAILED(gfx.dc->CreateBitmapFromDxgiSurface(surface.Get(), &props, gfx.backBuffer.GetAddressOf())))
        return false;

    gfx.dc->SetTarget(gfx.backBuffer.Get());
    gfx.pixelWidth = width;
    gfx.pixelHeight = height;
    gfx.dpi = dpi;
    return true;
}

}  // namespace

bool GfxCreate(Gfx& gfx, HWND hwnd, UINT width, UINT height, float dpi)
{
    if (!CreateDeviceAndSwapChain(gfx, width, height)) return false;

    gfx.composition->CreateTargetForHwnd(hwnd, TRUE, gfx.compositionTarget.GetAddressOf());
    gfx.composition->CreateVisual(gfx.compositionVisual.GetAddressOf());
    gfx.compositionVisual->SetContent(gfx.swapChain.Get());
    gfx.compositionTarget->SetRoot(gfx.compositionVisual.Get());
    gfx.composition->Commit();

    if (!CreateBackBuffer(gfx, width, height, dpi)) return false;

    gfx.dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    gfx.dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    gfx.dc->SetUnitMode(D2D1_UNIT_MODE_DIPS);
    return true;
}

bool GfxResize(Gfx& gfx, UINT width, UINT height, float dpi)
{
    if (!gfx.swapChain) return false;
    if (width == 0 || height == 0) return true;
    gfx.dc->SetTarget(nullptr);
    gfx.backBuffer.Reset();
    if (FAILED(gfx.swapChain->ResizeBuffers(2, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, 0))) return false;
    return CreateBackBuffer(gfx, width, height, dpi);
}

void GfxBegin(Gfx& gfx)
{
    gfx.dc->BeginDraw();
    gfx.dc->SetTransform(D2D1::Matrix3x2F::Identity());
    gfx.dc->Clear(D2D1::ColorF(0, 0.0f));
}

void GfxEnd(Gfx& gfx)
{
    gfx.dc->EndDraw();
    gfx.swapChain->Present(1, 0);
}

void GfxDestroy(Gfx& gfx)
{
    if (gfx.dc) gfx.dc->SetTarget(nullptr);
    gfx.backBuffer.Reset();
    gfx.compositionVisual.Reset();
    gfx.compositionTarget.Reset();
    gfx.composition.Reset();
    gfx.swapChain.Reset();
    gfx.dc.Reset();
    gfx.d2dDevice.Reset();
    gfx.d2dFactory.Reset();
    gfx.d3dContext.Reset();
    gfx.d3d.Reset();
}

bool RenderToPng(ID2D1Factory* factory, const std::wstring& path, UINT width, UINT height,
                 D2D1_COLOR_F background, const std::function<void(ID2D1RenderTarget*)>& draw)
{
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(wic.GetAddressOf()))))
        return false;

    ComPtr<IWICBitmap> bitmap;
    if (FAILED(wic->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                                 bitmap.GetAddressOf())))
        return false;

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);

    ComPtr<ID2D1RenderTarget> target;
    if (FAILED(factory->CreateWicBitmapRenderTarget(bitmap.Get(), props, target.GetAddressOf())))
        return false;

    target->BeginDraw();
    target->SetTransform(D2D1::Matrix3x2F::Identity());
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    target->Clear(background);
    draw(target.Get());
    if (FAILED(target->EndDraw())) return false;

    ComPtr<IWICStream> stream;
    if (FAILED(wic->CreateStream(stream.GetAddressOf()))) return false;
    if (FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return false;

    ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf()))) return false;
    if (FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return false;

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> frameProps;
    if (FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), frameProps.GetAddressOf()))) return false;
    if (FAILED(frame->Initialize(frameProps.Get()))) return false;
    if (FAILED(frame->SetSize(width, height))) return false;

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    if (FAILED(frame->SetPixelFormat(&format))) return false;

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(wic->CreateFormatConverter(converter.GetAddressOf()))) return false;
    if (FAILED(converter->Initialize(bitmap.Get(), format, WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom)))
        return false;

    if (FAILED(frame->WriteSource(converter.Get(), nullptr))) return false;
    if (FAILED(frame->Commit())) return false;
    if (FAILED(encoder->Commit())) return false;
    return true;
}

bool CaptureWindowToPng(HWND hwnd, const std::wstring& path)
{
    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) return false;
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) return false;

    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    if (!memory || !bitmap)
    {
        if (memory) DeleteDC(memory);
        if (bitmap) DeleteObject(bitmap);
        ReleaseDC(nullptr, screen);
        return false;
    }
    HGDIOBJ old = SelectObject(memory, bitmap);
    BitBlt(memory, 0, 0, width, height, screen, rect.left, rect.top, SRCCOPY);
    SelectObject(memory, old);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    std::vector<BYTE> pixels(static_cast<size_t>(width) * height * 4);
    int lines = GetDIBits(memory, bitmap, 0, static_cast<UINT>(height), pixels.data(), &info, DIB_RGB_COLORS);

    DeleteDC(memory);
    DeleteObject(bitmap);
    ReleaseDC(nullptr, screen);
    if (lines == 0) return false;

    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(wic.GetAddressOf()))))
        return false;

    ComPtr<IWICBitmap> wicBitmap;
    if (FAILED(wic->CreateBitmapFromMemory(static_cast<UINT>(width), static_cast<UINT>(height),
                                           GUID_WICPixelFormat32bppBGR, static_cast<UINT>(width) * 4,
                                           static_cast<UINT>(pixels.size()), pixels.data(),
                                           wicBitmap.GetAddressOf())))
        return false;

    ComPtr<IWICStream> stream;
    if (FAILED(wic->CreateStream(stream.GetAddressOf()))) return false;
    if (FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return false;
    ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf()))) return false;
    if (FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return false;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    if (FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), props.GetAddressOf()))) return false;
    if (FAILED(frame->Initialize(props.Get()))) return false;
    if (FAILED(frame->SetSize(width, height))) return false;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGR;
    if (FAILED(frame->SetPixelFormat(&format))) return false;
    if (FAILED(frame->WriteSource(wicBitmap.Get(), nullptr))) return false;
    if (FAILED(frame->Commit())) return false;
    return SUCCEEDED(encoder->Commit());
}
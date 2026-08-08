#include "aoc/platform/renderer.h"

#include <algorithm>
#include <cmath>

namespace aoc::platform {
using Microsoft::WRL::ComPtr;

LayeredRenderer::~LayeredRenderer() { reset(); }

bool LayeredRenderer::initialize() {
    if (d2dFactory_ && writeFactory_ && dcRenderTarget_) return true;
    HRESULT result = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory_.GetAddressOf());
    if (FAILED(result)) return false;
    result = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(writeFactory_.GetAddressOf()));
    if (FAILED(result)) return false;
    const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    result = d2dFactory_->CreateDCRenderTarget(&properties, dcRenderTarget_.GetAddressOf());
    return SUCCEEDED(result);
}

bool LayeredRenderer::ensureSurface(SIZE pixelSize) {
    pixelSize.cx = std::max<LONG>(1, pixelSize.cx);
    pixelSize.cy = std::max<LONG>(1, pixelSize.cy);
    if (memoryDc_ && bitmap_ && pixelSize.cx == pixelSize_.cx && pixelSize.cy == pixelSize_.cy) return true;
    if (memoryDc_) {
        if (oldBitmap_) SelectObject(memoryDc_, oldBitmap_);
        if (bitmap_) DeleteObject(bitmap_);
        DeleteDC(memoryDc_);
        memoryDc_ = nullptr;
        bitmap_ = nullptr;
        oldBitmap_ = nullptr;
        bits_ = nullptr;
    }
    memoryDc_ = CreateCompatibleDC(nullptr);
    if (!memoryDc_) return false;
    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = pixelSize.cx;
    bitmapInfo.bmiHeader.biHeight = -pixelSize.cy;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    bitmap_ = CreateDIBSection(memoryDc_, &bitmapInfo, DIB_RGB_COLORS, &bits_, nullptr, 0);
    if (!bitmap_) {
        DeleteDC(memoryDc_);
        memoryDc_ = nullptr;
        return false;
    }
    oldBitmap_ = static_cast<HBITMAP>(SelectObject(memoryDc_, bitmap_));
    pixelSize_ = pixelSize;
    return true;
}

bool LayeredRenderer::renderText(const std::wstring& text,
                                 double fontSizeDip,
                                 core::Color color,
                                 double opacity,
                                 std::uint32_t dpi,
                                 core::SizeD& measuredDip) {
    if (!initialize() || text.empty()) return false;
    const float safeFontSize = static_cast<float>(std::clamp(fontSizeDip, 8.0, 128.0));
    HRESULT result = writeFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                                     DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                     safeFontSize, L"", textFormat_.ReleaseAndGetAddressOf());
    if (FAILED(result)) return false;
    textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    textFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    ComPtr<IDWriteTextLayout> layout;
    result = writeFactory_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), textFormat_.Get(),
                                             4096.0f, 4096.0f, layout.GetAddressOf());
    if (FAILED(result)) return false;
    DWRITE_TEXT_METRICS metrics{};
    result = layout->GetMetrics(&metrics);
    if (FAILED(result)) return false;
    measuredDip = {std::max(1.0, static_cast<double>(metrics.widthIncludingTrailingWhitespace) + 2.0),
                   std::max(1.0, static_cast<double>(metrics.height) + 2.0)};
    const SIZE pixelSize{static_cast<LONG>(std::ceil(measuredDip.width * dpi / 96.0)),
                         static_cast<LONG>(std::ceil(measuredDip.height * dpi / 96.0))};
    if (!ensureSurface(pixelSize)) return false;
    RECT renderRect{0, 0, pixelSize.cx, pixelSize.cy};
    result = dcRenderTarget_->BindDC(memoryDc_, &renderRect);
    if (FAILED(result)) return false;
    dcRenderTarget_->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
    dcRenderTarget_->BeginDraw();
    dcRenderTarget_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    ComPtr<ID2D1SolidColorBrush> brush;
    const float alpha = static_cast<float>(std::clamp(opacity, 0.0, 1.0) * color.a / 255.0);
    result = dcRenderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, alpha), brush.GetAddressOf());
    if (SUCCEEDED(result)) {
        dcRenderTarget_->DrawTextLayout(D2D1::Point2F(1.0f, 1.0f), layout.Get(), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    }
    const HRESULT drawResult = dcRenderTarget_->EndDraw();
    return SUCCEEDED(result) && SUCCEEDED(drawResult);
}

bool LayeredRenderer::present(HWND window, POINT screenPosition) {
    if (!window || !memoryDc_ || !pixelSize_.cx || !pixelSize_.cy) return false;
    HDC screenDc = GetDC(nullptr);
    if (!screenDc) return false;
    POINT source{0, 0};
    SIZE size = pixelSize_;
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    const BOOL success = UpdateLayeredWindow(window, screenDc, &screenPosition, &size, memoryDc_, &source,
                                             0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screenDc);
    return success != FALSE;
}

void LayeredRenderer::reset() {
    if (memoryDc_) {
        if (oldBitmap_) SelectObject(memoryDc_, oldBitmap_);
        if (bitmap_) DeleteObject(bitmap_);
        DeleteDC(memoryDc_);
    }
    memoryDc_ = nullptr;
    bitmap_ = nullptr;
    oldBitmap_ = nullptr;
    bits_ = nullptr;
    pixelSize_ = {};
    textFormat_.Reset();
    dcRenderTarget_.Reset();
    writeFactory_.Reset();
    d2dFactory_.Reset();
}

} // namespace aoc::platform

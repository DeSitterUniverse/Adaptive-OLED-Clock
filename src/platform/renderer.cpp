#include "aoc/platform/renderer.h"

#include <algorithm>
#include <cmath>

namespace aoc::platform {
using Microsoft::WRL::ComPtr;

LayeredRenderer::~LayeredRenderer() { reset(); }

bool LayeredRenderer::initialize() {
    if (d2dFactory_ && writeFactory_ && dcRenderTarget_) return true;
    HRESULT result = S_OK;
    if (!d2dFactory_) {
        result = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                   d2dFactory_.ReleaseAndGetAddressOf());
        if (FAILED(result)) return false;
    }
    if (!writeFactory_) {
        result = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                     reinterpret_cast<IUnknown**>(writeFactory_.ReleaseAndGetAddressOf()));
        if (FAILED(result)) return false;
    }
    const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    result = d2dFactory_->CreateDCRenderTarget(&properties, dcRenderTarget_.ReleaseAndGetAddressOf());
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
                                 const std::wstring& fontFamily,
                                 core::FontWeight fontWeight,
                                 double fontSizeDip,
                                 core::Color color,
                                 double opacity,
                                 std::uint32_t dpi,
                                 bool positioning,
                                 core::SizeD& measuredTextDip,
                                 core::SizeD& measuredSurfaceDip) {
    if (!initialize() || text.empty()) return false;
    const float safeFontSize = static_cast<float>(std::clamp(fontSizeDip, 8.0, 128.0));
    const DWRITE_FONT_WEIGHT weight = fontWeight == core::FontWeight::SemiBold
                                          ? DWRITE_FONT_WEIGHT_SEMI_BOLD
                                          : DWRITE_FONT_WEIGHT_NORMAL;
    const std::wstring family = fontFamily.empty() ? L"Segoe UI" : fontFamily;
    HRESULT result = S_OK;
    if (!textFormat_ || family != cachedFontFamily_ || fontWeight != cachedFontWeight_ ||
        std::abs(fontSizeDip - cachedFontSizeDip_) > 0.005) {
        result = writeFactory_->CreateTextFormat(family.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                                 DWRITE_FONT_STRETCH_NORMAL, safeFontSize, L"",
                                                 textFormat_.ReleaseAndGetAddressOf());
        if (FAILED(result)) return false;
        textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        textFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        cachedFontFamily_ = family;
        cachedFontWeight_ = fontWeight;
        cachedFontSizeDip_ = fontSizeDip;
    }

    ComPtr<IDWriteTextLayout> clockLayout;
    result = writeFactory_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), textFormat_.Get(),
                                             4096.0f, 4096.0f, clockLayout.GetAddressOf());
    if (FAILED(result)) return false;
    DWRITE_TEXT_METRICS clockMetrics{};
    result = clockLayout->GetMetrics(&clockMetrics);
    if (FAILED(result)) return false;
    measuredTextDip = {std::max(1.0, static_cast<double>(clockMetrics.widthIncludingTrailingWhitespace) + 2.0),
                       std::max(1.0, static_cast<double>(clockMetrics.height) + 2.0)};

    constexpr double paddingDip = 16.0;
    double contentWidth = measuredTextDip.width;
    double contentHeight = measuredTextDip.height;
    ComPtr<IDWriteTextLayout> instructionLayout;
    if (positioning) {
        if (!instructionFormat_) {
            result = writeFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                                     DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0f,
                                                     L"", instructionFormat_.ReleaseAndGetAddressOf());
            if (FAILED(result)) return false;
            instructionFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        }
        constexpr wchar_t instruction[] = L"Drag anywhere  /  Esc to finish";
        result = writeFactory_->CreateTextLayout(instruction,
                                                 static_cast<UINT32>(std::size(instruction) - 1),
                                                 instructionFormat_.Get(),
                                                 static_cast<float>(measuredTextDip.width), 4096.0f,
                                                 instructionLayout.GetAddressOf());
        if (FAILED(result)) return false;
        DWRITE_TEXT_METRICS instructionMetrics{};
        result = instructionLayout->GetMetrics(&instructionMetrics);
        if (FAILED(result)) return false;
        contentWidth = std::max(contentWidth, static_cast<double>(instructionMetrics.widthIncludingTrailingWhitespace));
        contentHeight += static_cast<double>(instructionMetrics.height) + 5.0;
    }
    const double surfacePadding = positioning ? paddingDip : 0.0;
    measuredSurfaceDip = {contentWidth + 2.0 * surfacePadding, contentHeight + 2.0 * surfacePadding};
    const SIZE pixelSize{static_cast<LONG>(std::ceil(measuredSurfaceDip.width * dpi / 96.0)),
                         static_cast<LONG>(std::ceil(measuredSurfaceDip.height * dpi / 96.0))};
    if (!ensureSurface(pixelSize)) return false;
    RECT renderRect{0, 0, pixelSize.cx, pixelSize.cy};
    result = dcRenderTarget_->BindDC(memoryDc_, &renderRect);
    if (FAILED(result)) return false;
    dcRenderTarget_->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
    dcRenderTarget_->BeginDraw();
    dcRenderTarget_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    const float textAlpha = static_cast<float>(std::clamp(opacity, 0.0, 1.0) * color.a / 255.0);
    const D2D1_COLOR_F brushColor = D2D1::ColorF(
        color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, textAlpha);
    if (!textBrush_) {
        result = dcRenderTarget_->CreateSolidColorBrush(brushColor, textBrush_.GetAddressOf());
    } else {
        textBrush_->SetColor(brushColor);
        result = S_OK;
    }
    HRESULT drawResult = result;
    if (SUCCEEDED(drawResult) && positioning) {
        ComPtr<ID2D1SolidColorBrush> cardBrush;
        ComPtr<ID2D1SolidColorBrush> borderBrush;
        drawResult = dcRenderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.16f, 0.18f, 0.21f, 0.14f),
                                                            cardBrush.GetAddressOf());
        if (SUCCEEDED(drawResult)) {
            drawResult = dcRenderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.65f, 0.72f, 0.80f, 0.42f),
                                                                borderBrush.GetAddressOf());
        }
        const D2D1_RECT_F card{0.5f, 0.5f, static_cast<float>(measuredSurfaceDip.width - 0.5),
                               static_cast<float>(measuredSurfaceDip.height - 0.5)};
        if (SUCCEEDED(drawResult)) dcRenderTarget_->FillRectangle(card, cardBrush.Get());
        if (SUCCEEDED(drawResult)) dcRenderTarget_->DrawRectangle(card, borderBrush.Get(), 1.0f);
    }
    const D2D1_POINT_2F textOrigin{static_cast<float>(surfacePadding + 1.0), static_cast<float>(surfacePadding + 1.0)};
    if (SUCCEEDED(drawResult)) dcRenderTarget_->DrawTextLayout(textOrigin, clockLayout.Get(), textBrush_.Get(),
                                                               D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    if (SUCCEEDED(drawResult) && positioning) {
        ComPtr<ID2D1SolidColorBrush> instructionBrush;
        drawResult = dcRenderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.78f, 0.82f, 0.88f, 0.62f),
                                                            instructionBrush.GetAddressOf());
        if (SUCCEEDED(drawResult)) {
            dcRenderTarget_->DrawTextLayout({static_cast<float>(surfacePadding + 1.0),
                                             static_cast<float>(surfacePadding + measuredTextDip.height + 2.0)},
                                            instructionLayout.Get(), instructionBrush.Get(),
                                            D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        }
    }
    const HRESULT endResult = dcRenderTarget_->EndDraw();
    if (endResult == D2DERR_RECREATE_TARGET) {
        // Device-dependent resources are invalid after display/driver loss.
        // Keep the factories and rebuild the target on the next frame.
        dcRenderTarget_.Reset();
        textBrush_.Reset();
        textFormat_.Reset();
        instructionFormat_.Reset();
        return false;
    }
    return SUCCEEDED(result) && SUCCEEDED(drawResult) && SUCCEEDED(endResult);
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
    instructionFormat_.Reset();
    textBrush_.Reset();
    cachedFontFamily_.clear();
    cachedFontSizeDip_ = 0.0;
    dcRenderTarget_.Reset();
    writeFactory_.Reset();
    d2dFactory_.Reset();
}

} // namespace aoc::platform

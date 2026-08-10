#pragma once

#include "aoc/core/types.h"

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <string>

namespace aoc::platform {

class LayeredRenderer {
public:
    LayeredRenderer() = default;
    ~LayeredRenderer();

    LayeredRenderer(const LayeredRenderer&) = delete;
    LayeredRenderer& operator=(const LayeredRenderer&) = delete;

    [[nodiscard]] bool initialize();
    [[nodiscard]] bool renderText(const std::wstring& text,
                                  const std::wstring& fontFamily,
                                  core::FontWeight fontWeight,
                                  double fontSizeDip,
                                  core::Color color,
                                  double opacity,
                                  std::uint32_t dpi,
                                  bool positioning,
                                  core::SizeD& measuredTextDip,
                                  core::SizeD& measuredSurfaceDip);
    [[nodiscard]] bool present(HWND window, POINT screenPosition);
    void reset();

private:
    [[nodiscard]] bool ensureSurface(SIZE pixelSize);

    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> writeFactory_;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> dcRenderTarget_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> instructionFormat_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush_;
    std::wstring cachedFontFamily_;
    core::FontWeight cachedFontWeight_{core::FontWeight::Normal};
    double cachedFontSizeDip_{0.0};
    HDC memoryDc_{nullptr};
    HBITMAP bitmap_{nullptr};
    HBITMAP oldBitmap_{nullptr};
    void* bits_{nullptr};
    SIZE pixelSize_{};
    core::SizeD measuredDip_{};
};

} // namespace aoc::platform

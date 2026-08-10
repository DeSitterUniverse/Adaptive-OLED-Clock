#pragma once

#include "aoc/core/color.h"
#include "aoc/platform/win32_raii.h"

#include <windows.h>

#include <array>
#include <optional>
#include <vector>

namespace aoc::platform {

class ColorPickerWindow {
public:
    [[nodiscard]] static std::optional<core::Color> choose(HINSTANCE instance, HWND owner,
                                                           core::Color initialColor);

private:
    ColorPickerWindow(HINSTANCE instance, HWND owner, core::Color initialColor);
    ~ColorPickerWindow();

    ColorPickerWindow(const ColorPickerWindow&) = delete;
    ColorPickerWindow& operator=(const ColorPickerWindow&) = delete;

    [[nodiscard]] bool create();
    [[nodiscard]] std::optional<core::Color> run();
    static LRESULT CALLBACK windowProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void layout(int width, int height);
    void paint(HDC dc);
    void paintGradient(HDC dc, const RECT& bounds) const;
    void paintHue(HDC dc, const RECT& bounds) const;
    void paintButton(const DRAWITEMSTRUCT& item) const;
    void applyTheme();
    void setFromPoint(POINT point);
    void setHueFromPoint(POINT point);
    void setColor(core::Color color, bool updateHex);
    void updateHexText(bool force = false);
    void readHexText();
    void finish(bool accepted);
    [[nodiscard]] RECT presetRect(std::size_t index) const noexcept;
    [[nodiscard]] int scale(int value) const noexcept;

    HINSTANCE instance_{nullptr};
    HWND owner_{nullptr};
    HWND hwnd_{nullptr};
    HWND hexEdit_{nullptr};
    HWND cancelButton_{nullptr};
    HWND acceptButton_{nullptr};
    UINT dpi_{96};
    UniqueGdiFont font_;
    UniqueGdiFont titleFont_;
    core::Color color_{};
    core::HsvColor hsv_{};
    bool hexValid_{true};
    bool syncingHex_{false};
    bool draggingField_{false};
    bool draggingHue_{false};
    bool finished_{false};
    bool accepted_{false};
    bool bufferedPaintInitialized_{false};
    ULONGLONG lastHexUpdateTick_{0};
    RECT fieldRect_{};
    RECT hueRect_{};
    RECT swatchRect_{};
    RECT hexContainer_{};
    RECT presetsRect_{};
    mutable std::vector<std::uint32_t> gradientPixels_;
    mutable std::vector<std::uint32_t> huePixels_;
    mutable int gradientWidth_{0};
    mutable int gradientHeight_{0};
    mutable int hueWidth_{0};
    mutable double gradientHue_{-1.0};
};

} // namespace aoc::platform

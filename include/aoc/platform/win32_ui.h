#pragma once

#include <windows.h>

#include <string>

namespace aoc::platform {

[[nodiscard]] int scaleDip(int value, UINT dpi) noexcept;
[[nodiscard]] HFONT createControlFont(UINT dpi) noexcept;
[[nodiscard]] HICON loadApplicationIcon(HINSTANCE instance, int width, int height) noexcept;
void setControlFont(HWND control, HFONT font, bool redraw = true) noexcept;
[[nodiscard]] RECT fitToWorkArea(RECT proposed) noexcept;
[[nodiscard]] RECT centeredWindowRect(HWND anchor, HWND window, int preferredWidthDip,
                                      int preferredHeightDip, UINT dpi) noexcept;
[[nodiscard]] std::wstring wideFromUtf8(const std::string& value);
[[nodiscard]] std::string utf8FromWide(const std::wstring& value);

} // namespace aoc::platform

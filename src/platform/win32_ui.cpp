#include "aoc/platform/win32_ui.h"

#include <algorithm>

namespace aoc::platform {

int scaleDip(int value, UINT dpi) noexcept {
    return MulDiv(value, dpi == 0 ? 96 : static_cast<int>(dpi), 96);
}

HFONT createControlFont(UINT dpi) noexcept {
    return CreateFontW(-scaleDip(14, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void setControlFont(HWND control, HFONT font, bool redraw) noexcept {
    if (control && font) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), redraw ? TRUE : FALSE);
    }
}

RECT fitToWorkArea(RECT proposed) noexcept {
    const HMONITOR monitor = MonitorFromRect(&proposed, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(MONITORINFO)};
    if (!monitor || !GetMonitorInfoW(monitor, &info)) return proposed;
    const RECT work = info.rcWork;
    const int width = std::min(std::max(1L, proposed.right - proposed.left), work.right - work.left);
    const int height = std::min(std::max(1L, proposed.bottom - proposed.top), work.bottom - work.top);
    proposed.left = work.left + std::clamp(proposed.left - work.left, 0L, work.right - work.left - width);
    proposed.top = work.top + std::clamp(proposed.top - work.top, 0L, work.bottom - work.top - height);
    proposed.right = proposed.left + width;
    proposed.bottom = proposed.top + height;
    return proposed;
}

RECT centeredWindowRect(HWND anchor, HWND window, int preferredWidthDip,
                        int preferredHeightDip, UINT dpi) noexcept {
    RECT workArea{};
    const HMONITOR monitor = MonitorFromWindow(anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(MONITORINFO)};
    if (monitor && GetMonitorInfoW(monitor, &info)) {
        workArea = info.rcWork;
    } else {
        workArea.right = GetSystemMetrics(SM_CXSCREEN);
        workArea.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    RECT current{};
    if (!window || !GetWindowRect(window, &current)) {
        current.right = scaleDip(preferredWidthDip, dpi);
        current.bottom = scaleDip(preferredHeightDip, dpi);
    }
    const int currentWidth = std::max(1L, current.right - current.left);
    const int currentHeight = std::max(1L, current.bottom - current.top);
    const int workWidth = std::max(1L, workArea.right - workArea.left);
    const int workHeight = std::max(1L, workArea.bottom - workArea.top);
    const int availableWidth = std::max(1, workWidth - scaleDip(24, dpi));
    const int availableHeight = std::max(1, workHeight - scaleDip(24, dpi));
    const int width = std::min(std::max(std::min(scaleDip(preferredWidthDip, dpi), availableWidth),
                                        currentWidth), availableWidth);
    const int height = std::min(std::max(std::min(scaleDip(preferredHeightDip, dpi), availableHeight),
                                         currentHeight), availableHeight);
    const int left = workArea.left + std::max(0, (workWidth - width) / 2);
    const int top = workArea.top + std::max(0, (workHeight - height) / 2);
    return RECT{left, top, left + width, top + height};
}

std::wstring wideFromUtf8(const std::string& value) {
    if (value.empty()) return {};
    const int required = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) return {};
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), required);
    return result;
}

std::string utf8FromWide(const std::wstring& value) {
    if (value.empty()) return {};
    const int required = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                              nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), required, nullptr, nullptr);
    return result;
}

} // namespace aoc::platform

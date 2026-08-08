#include "aoc/platform/fullscreen_service.h"

#include <windows.h>
#include <dwmapi.h>

#include <array>

namespace aoc::platform {
namespace {

bool isShellWindow(HWND window) {
    if (!window || window == GetShellWindow()) return true;
    wchar_t className[128]{};
    GetClassNameW(window, className, static_cast<int>(std::size(className)));
    return wcscmp(className, L"Shell_TrayWnd") == 0 || wcscmp(className, L"WorkerW") == 0 ||
           wcscmp(className, L"Progman") == 0;
}

} // namespace

core::ForegroundWindowSnapshot captureForegroundSnapshot() {
    core::ForegroundWindowSnapshot snapshot;
    const HWND foreground = GetForegroundWindow();
    if (!foreground) return snapshot;
    snapshot.visible = IsWindowVisible(foreground) != FALSE;
    snapshot.minimized = IsIconic(foreground) != FALSE;
    snapshot.shellWindow = isShellWindow(foreground);
    snapshot.toolWindow = (GetWindowLongPtrW(foreground, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0;
    RECT windowRect{};
    if (GetWindowRect(foreground, &windowRect)) {
        snapshot.windowRectPx = {windowRect.left, windowRect.top, windowRect.right, windowRect.bottom};
    }
    RECT extended{};
    if (SUCCEEDED(DwmGetWindowAttribute(foreground, DWMWA_EXTENDED_FRAME_BOUNDS, &extended, sizeof(extended)))) {
        snapshot.extendedFrameRectPx = {extended.left, extended.top, extended.right, extended.bottom};
    }
    const HMONITOR monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{sizeof(MONITORINFO)};
    if (monitor && GetMonitorInfoW(monitor, &monitorInfo)) {
        snapshot.monitorBoundsPx = {monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.top,
                                    monitorInfo.rcMonitor.right, monitorInfo.rcMonitor.bottom};
    }
    return snapshot;
}

} // namespace aoc::platform

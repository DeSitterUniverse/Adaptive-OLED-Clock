#include "aoc/platform/monitor_service.h"

#include <windows.h>
#include <shellscalingapi.h>

#include <algorithm>
#include <string>

namespace aoc::platform {
namespace {

struct EnumerationContext {
    std::vector<core::MonitorInfo>* monitors;
};

std::string utf8(const wchar_t* value) {
    if (!value) return {};
    const int required = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) return {};
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), required, nullptr, nullptr);
    result.resize(static_cast<std::size_t>(required - 1));
    return result;
}

BOOL CALLBACK monitorCallback(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    auto& context = *reinterpret_cast<EnumerationContext*>(data);
    MONITORINFOEXW monitorInfo{sizeof(MONITORINFOEXW)};
    if (!GetMonitorInfoW(monitor, &monitorInfo)) return TRUE;
    core::MonitorInfo info;
    info.displayName = monitorInfo.szDevice;
    info.boundsPx = {monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.top,
                     monitorInfo.rcMonitor.right, monitorInfo.rcMonitor.bottom};
    info.workAreaPx = {monitorInfo.rcWork.left, monitorInfo.rcWork.top,
                       monitorInfo.rcWork.right, monitorInfo.rcWork.bottom};
    info.primary = (monitorInfo.dwFlags & MONITORINFOF_PRIMARY) != 0;
    // Query the monitor attached to this GDI display, not the display adapter.
    // EDD_GET_DEVICE_INTERFACE_NAME returns the per-monitor device-interface
    // path, which remains distinct when several panels share one GPU.
    DISPLAY_DEVICEW monitorDevice{sizeof(DISPLAY_DEVICEW)};
    if (EnumDisplayDevicesW(monitorInfo.szDevice, 0, &monitorDevice,
                            EDD_GET_DEVICE_INTERFACE_NAME) != FALSE) {
        info.stableKey = utf8(monitorDevice.DeviceID);
        if (monitorDevice.DeviceString[0] != L'\0') info.displayName = monitorDevice.DeviceString;
    }
    if (info.stableKey.empty()) {
        // Topology names are a fallback only. Include geometry to prevent two
        // active monitors from ever sharing one exposure map.
        info.stableKey = utf8(monitorInfo.szDevice) + "@" +
                         std::to_string(info.boundsPx.left) + "," +
                         std::to_string(info.boundsPx.top) + "," +
                         std::to_string(info.boundsPx.right) + "," +
                         std::to_string(info.boundsPx.bottom);
    }
    UINT dpiX = 96;
    UINT dpiY = 96;
    if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
        info.dpiX = dpiX;
        info.dpiY = dpiY;
    }
    context.monitors->push_back(std::move(info));
    return TRUE;
}

} // namespace

std::vector<core::MonitorInfo> MonitorService::enumerate() const {
    std::vector<core::MonitorInfo> monitors;
    EnumerationContext context{&monitors};
    EnumDisplayMonitors(nullptr, nullptr, monitorCallback, reinterpret_cast<LPARAM>(&context));
    return monitors;
}

} // namespace aoc::platform

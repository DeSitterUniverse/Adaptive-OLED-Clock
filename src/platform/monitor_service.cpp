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
    DISPLAY_DEVICEW displayDevice{sizeof(DISPLAY_DEVICEW)};
    for (DWORD index = 0; EnumDisplayDevicesW(nullptr, index, &displayDevice, 0); ++index) {
        if (wcscmp(displayDevice.DeviceName, monitorInfo.szDevice) == 0) {
            info.stableKey = utf8(displayDevice.DeviceID);
            break;
        }
        displayDevice = DISPLAY_DEVICEW{sizeof(DISPLAY_DEVICEW)};
    }
    if (info.stableKey.empty()) {
        info.stableKey = utf8(monitorInfo.szDevice);
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

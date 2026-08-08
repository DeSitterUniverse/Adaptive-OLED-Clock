#include "aoc/core/monitor.h"

namespace aoc::core {

MonitorSelection selectMonitor(const std::vector<MonitorInfo>& monitors,
                               const Settings& settings) {
    MonitorSelection result;
    if (monitors.empty()) return result;
    const auto primary = std::find_if(monitors.begin(), monitors.end(), [](const MonitorInfo& monitor) {
        return monitor.primary;
    });
    const MonitorInfo& primaryMonitor = primary == monitors.end() ? monitors.front() : *primary;
    if (settings.monitorMode == MonitorMode::Fixed && !settings.fixedMonitorKey.empty()) {
        const auto fixed = std::find_if(monitors.begin(), monitors.end(), [&](const MonitorInfo& monitor) {
            return monitor.stableKey == settings.fixedMonitorKey;
        });
        if (fixed != monitors.end()) {
            result.selected = *fixed;
            return result;
        }
        result.usedPrimaryFallback = true;
    }
    result.selected = primaryMonitor;
    return result;
}

} // namespace aoc::core

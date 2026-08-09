#include "aoc/core/settings_change.h"

namespace aoc::core {

SettingsChange classifySettingsChange(const Settings& previous, const Settings& current) noexcept {
    SettingsChange change;
    change.secondsChanged = previous.showSeconds != current.showSeconds;
    change.appearanceChanged = previous.timeFormat != current.timeFormat ||
                               previous.showAmPm != current.showAmPm ||
                               previous.showSeconds != current.showSeconds ||
                               previous.showDate != current.showDate ||
                               previous.fontFamily != current.fontFamily ||
                               previous.fontWeight != current.fontWeight ||
                               previous.fontSizeDip != current.fontSizeDip ||
                               previous.textColor != current.textColor ||
                               previous.opacity != current.opacity ||
                               previous.boostOpacity != current.boostOpacity;
    change.movementModeChanged = previous.movementMode != current.movementMode;
    change.placementPolicyChanged = change.movementModeChanged ||
                                    previous.allowedArea != current.allowedArea ||
                                    previous.edgeMarginDip != current.edgeMarginDip ||
                                    previous.localAreaRadiusPx != current.localAreaRadiusPx ||
                                    previous.localAreaAnchor != current.localAreaAnchor ||
                                    previous.localAreaAnchorSet != current.localAreaAnchorSet;
    change.monitorSelectionChanged = previous.monitorMode != current.monitorMode ||
                                     previous.fixedMonitorKey != current.fixedMonitorKey;
    change.movementIntervalChanged = previous.movementIntervalSeconds != current.movementIntervalSeconds;
    change.microShiftChanged = previous.microShiftEnabled != current.microShiftEnabled ||
                               previous.microShiftCount != current.microShiftCount ||
                               previous.microShiftDistancePx != current.microShiftDistancePx;
    change.hotkeyChanged = previous.hotkeyEnabled != current.hotkeyEnabled;
    change.startupChanged = previous.launchAtStartup != current.launchAtStartup;
    change.anyChanged = previous.version != current.version || change.appearanceChanged ||
                        change.placementPolicyChanged || change.monitorSelectionChanged ||
                        change.movementIntervalChanged || change.microShiftChanged ||
                        previous.hideInFullscreen != current.hideInFullscreen ||
                        change.startupChanged || change.hotkeyChanged ||
                        previous.clockVisible != current.clockVisible ||
                        previous.boostDurationSeconds != current.boostDurationSeconds;
    return change;
}

} // namespace aoc::core

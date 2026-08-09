#pragma once

#include "aoc/core/settings.h"

namespace aoc::core {

struct SettingsChange {
    bool secondsChanged{false};
    bool appearanceChanged{false};
    bool movementModeChanged{false};
    bool placementPolicyChanged{false};
    bool monitorSelectionChanged{false};
    bool movementIntervalChanged{false};
    bool hotkeyChanged{false};
    bool startupChanged{false};
    bool anyChanged{false};
};

[[nodiscard]] SettingsChange classifySettingsChange(const Settings& previous,
                                                    const Settings& current) noexcept;

} // namespace aoc::core

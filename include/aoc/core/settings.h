#pragma once

#include "aoc/core/types.h"

#include <string>
#include <vector>

namespace aoc::core {

constexpr int kCurrentSettingsVersion = 1;

struct Settings {
    int version{kCurrentSettingsVersion};
    TimeFormat timeFormat{TimeFormat::Locale};
    bool showAmPm{true};
    double fontSizeDip{32.0};
    Color textColor{176, 176, 176, 255};
    double opacity{0.32};
    int movementIntervalMinutes{5};
    MovementMode movementMode{MovementMode::WholeScreen};
    NormalizedRect allowedArea{};
    std::vector<NormalizedRect> excludedAreas;
    double edgeMarginDip{24.0};
    MonitorMode monitorMode{MonitorMode::FollowPrimary};
    std::string fixedMonitorKey;
    NormalizedPoint preferredPosition{0.5, 0.5};
    bool hideInFullscreen{true};
    bool launchAtStartup{false};
    bool hotkeyEnabled{true};
    bool clockVisible{true};
    double boostOpacity{0.85};
    int boostDurationSeconds{15};

    [[nodiscard]] static Settings defaults();
    [[nodiscard]] static Settings oledSafePreset();
    void validateAndNormalize() noexcept;
};

struct SettingsLoadResult {
    Settings value{};
    bool recovered{false};
    std::string error;
};

[[nodiscard]] std::string serializeSettings(const Settings& settings);
[[nodiscard]] SettingsLoadResult deserializeSettings(const std::string& text);

} // namespace aoc::core

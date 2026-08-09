#pragma once

#include "aoc/core/types.h"

#include <string>

namespace aoc::core {

constexpr int kCurrentSettingsVersion = 6;

struct Settings {
    int version{kCurrentSettingsVersion};
    TimeFormat timeFormat{TimeFormat::Locale};
    bool showAmPm{true};
    bool showSeconds{false};
    bool showDate{false};
    std::string fontFamily{"Segoe UI"};
    FontWeight fontWeight{FontWeight::Normal};
    double fontSizeDip{32.0};
    Color textColor{255, 255, 255, 255};
    double opacity{0.80};
    int movementIntervalSeconds{3600};
    MovementMode movementMode{MovementMode::EdgeOnly};
    bool microShiftEnabled{true};
    int microShiftCount{3};
    int microShiftDistancePx{3};
    int localAreaRadiusPx{100};
    NormalizedPoint localAreaAnchor{};
    bool localAreaAnchorSet{false};
    NormalizedRect allowedArea{};
    double edgeMarginDip{0.0};
    MonitorMode monitorMode{MonitorMode::FollowPrimary};
    std::string fixedMonitorKey;
    bool hideInFullscreen{true};
    bool launchAtStartup{false};
    bool hotkeyEnabled{true};
    bool clockVisible{true};
    double boostOpacity{1.0};
    int boostDurationSeconds{15};

    [[nodiscard]] static Settings defaults();
    [[nodiscard]] static Settings oledPreset();
    void validateAndNormalize() noexcept;
};

struct SettingsLoadResult {
    Settings value{};
    bool recovered{false};
    bool migrated{false};
    std::string error;
};

[[nodiscard]] std::string serializeSettings(const Settings& settings);
[[nodiscard]] SettingsLoadResult deserializeSettings(const std::string& text);

} // namespace aoc::core

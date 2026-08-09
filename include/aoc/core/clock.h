#pragma once

#include "aoc/core/types.h"

#include <chrono>
#include <ctime>
#include <string>

namespace aoc::core {

enum class LocaleHourMode : std::uint8_t {
    TwelveHour,
    TwentyFourHour,
};

[[nodiscard]] bool useTwelveHourClock(TimeFormat configured, LocaleHourMode localeMode) noexcept;
[[nodiscard]] std::wstring formatClockText(const std::tm& localTime,
                                            TimeFormat configured,
                                            bool showAmPm,
                                            LocaleHourMode localeMode,
                                            bool showSeconds = false);
[[nodiscard]] std::chrono::system_clock::time_point nextMinuteBoundary(
    std::chrono::system_clock::time_point now);
[[nodiscard]] std::chrono::system_clock::time_point nextSecondBoundary(
    std::chrono::system_clock::time_point now);
[[nodiscard]] std::chrono::milliseconds evenlySpacedShiftOffset(int movementIntervalSeconds,
                                                                 int shiftCount,
                                                                 int zeroBasedShiftIndex) noexcept;
[[nodiscard]] LocaleHourMode localeHourModeFromPattern(const std::wstring& shortTimePattern) noexcept;

} // namespace aoc::core

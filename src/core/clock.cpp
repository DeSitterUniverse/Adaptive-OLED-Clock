#include "aoc/core/clock.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace aoc::core {

bool useTwelveHourClock(TimeFormat configured, LocaleHourMode localeMode) noexcept {
    switch (configured) {
    case TimeFormat::TwelveHour:
        return true;
    case TimeFormat::TwentyFourHour:
        return false;
    case TimeFormat::Locale:
    default:
        return localeMode == LocaleHourMode::TwelveHour;
    }
}

std::wstring formatClockText(const std::tm& localTime,
                             TimeFormat configured,
                             bool showAmPm,
                             LocaleHourMode localeMode) {
    const bool twelveHour = useTwelveHourClock(configured, localeMode);
    int hour = localTime.tm_hour;
    std::wstring suffix;
    if (twelveHour) {
        suffix = localTime.tm_hour >= 12 ? L" PM" : L" AM";
        hour %= 12;
        if (hour == 0) hour = 12;
    }
    std::wostringstream output;
    output << std::setfill(L'0');
    if (twelveHour) {
        output << hour;
    } else {
        output << std::setw(2) << hour;
    }
    output << L':' << std::setw(2) << std::clamp(localTime.tm_min, 0, 59);
    if (twelveHour && showAmPm) output << suffix;
    return output.str();
}

std::chrono::system_clock::time_point nextMinuteBoundary(
    std::chrono::system_clock::time_point now) {
    const auto minute = std::chrono::time_point_cast<std::chrono::minutes>(now);
    return minute + std::chrono::minutes(1);
}

LocaleHourMode localeHourModeFromPattern(const std::wstring& shortTimePattern) noexcept {
    if (shortTimePattern.find(L'h') != std::wstring::npos ||
        shortTimePattern.find(L't') != std::wstring::npos) {
        return LocaleHourMode::TwelveHour;
    }
    return LocaleHourMode::TwentyFourHour;
}

} // namespace aoc::core

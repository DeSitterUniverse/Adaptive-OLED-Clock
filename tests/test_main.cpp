#include "aoc/core/clock.h"
#include "aoc/core/exposure.h"
#include "aoc/core/fullscreen.h"
#include "aoc/core/geometry.h"
#include "aoc/core/monitor.h"
#include "aoc/core/placement.h"
#include "aoc/core/settings.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char* expression) {
    if (!condition) {
        std::cerr << "FAIL: " << expression << '\n';
        ++failures;
    }
}

#define CHECK(condition) check((condition), #condition)

void testClock() {
    std::tm time{};
    time.tm_hour = 0;
    time.tm_min = 7;
    CHECK(aoc::core::formatClockText(time, aoc::core::TimeFormat::TwelveHour, true,
                                     aoc::core::LocaleHourMode::TwentyFourHour) == L"12:07 AM");
    time.tm_hour = 13;
    CHECK(aoc::core::formatClockText(time, aoc::core::TimeFormat::TwentyFourHour, true,
                                     aoc::core::LocaleHourMode::TwelveHour) == L"13:07");
    time.tm_hour = 4;
    time.tm_min = 58;
    CHECK(aoc::core::formatClockText(time, aoc::core::TimeFormat::TwelveHour, true,
                                     aoc::core::LocaleHourMode::TwentyFourHour) == L"4:58 AM");
    CHECK(aoc::core::formatClockText(time, aoc::core::TimeFormat::TwelveHour, false,
                                     aoc::core::LocaleHourMode::TwentyFourHour) == L"4:58");
    CHECK(aoc::core::useTwelveHourClock(aoc::core::TimeFormat::Locale,
                                        aoc::core::LocaleHourMode::TwelveHour));
    const auto now = std::chrono::system_clock::time_point(std::chrono::seconds(10 * 3600 + 15 * 60 + 30));
    const auto next = aoc::core::nextMinuteBoundary(now);
    CHECK(std::chrono::duration_cast<std::chrono::seconds>(next.time_since_epoch()).count() == 10 * 3600 + 16 * 60);
    CHECK(aoc::core::localeHourModeFromPattern(L"h:mm tt") == aoc::core::LocaleHourMode::TwelveHour);
    CHECK(aoc::core::localeHourModeFromPattern(L"HH:mm") == aoc::core::LocaleHourMode::TwentyFourHour);
}

void testExposure() {
    aoc::core::ExposureMap map;
    map.charge({0.0, 0.0, 1.0, 1.0}, 60.0);
    CHECK(std::abs(map.totalSeconds() - 60.0) < 1e-9);
    CHECK(std::abs(map.cell(0, 0) - 60.0 / aoc::core::kExposureCellCount) < 1e-9);
    aoc::core::ExposureMap weighted;
    weighted.charge({0.0, 0.0, 0.25, 0.25}, 20.0);
    CHECK(std::abs(weighted.weightedExposure({0.0, 0.0, 0.25, 0.25}) - 20.0) < 1e-9);

    aoc::core::ExposureStore store;
    aoc::core::ExposureTracker tracker(store);
    const auto start = std::chrono::steady_clock::time_point{};
    tracker.setState("MONITOR-A", {0.0, 0.0, 0.25, 0.25}, true, start);
    tracker.checkpoint(start + std::chrono::seconds(30));
    tracker.setState("MONITOR-A", {0.0, 0.0, 0.25, 0.25}, false, start + std::chrono::seconds(30));
    tracker.checkpoint(start + std::chrono::seconds(90));
    CHECK(std::abs(store.forMonitor("MONITOR-A").totalSeconds() - 30.0) < 1e-9);
    tracker.setState("MONITOR-B", {0.5, 0.5, 0.75, 0.75}, true, start + std::chrono::seconds(90));
    tracker.settle(start + std::chrono::seconds(100));
    CHECK(std::abs(store.forMonitor("MONITOR-B").totalSeconds() - 10.0) < 1e-9);
    CHECK(std::abs(store.forMonitor("MONITOR-A").totalSeconds() - 30.0) < 1e-9);

    const std::string serialized = aoc::core::serializeExposure(store);
    const auto restored = aoc::core::deserializeExposure(serialized);
    CHECK(!restored.recovered);
    CHECK(restored.value.find("MONITOR-A") != nullptr);
    CHECK(std::abs(restored.value.find("MONITOR-B")->totalSeconds() - 10.0) < 1e-9);
    CHECK(aoc::core::deserializeExposure("version=1\nmonitorCount=1\nmonitor.0.key=broken\n").recovered);
}

void testGeometry() {
    const aoc::core::RectI monitor{100, 50, 2100, 1550};
    const auto physical = aoc::core::normalizedToPhysicalPixels({0.25, 0.20, 0.75, 0.80}, monitor);
    CHECK((physical == aoc::core::RectI{600, 350, 1600, 1250}));
    const auto normalized = aoc::core::physicalToNormalized(physical, monitor);
    CHECK(std::abs(normalized.left - 0.25) < 1e-9);
    CHECK(std::abs(aoc::core::dipToPixels(32.0, 144) - 48.0) < 1e-9);
    CHECK(std::abs(aoc::core::pixelsToDip(48.0, 144) - 32.0) < 1e-9);
    const auto dip = aoc::core::pixelsToDip(aoc::core::RectI{0, 0, 1440, 900}, 144);
    CHECK(std::abs(dip.width() - 960.0) < 1e-9 && std::abs(dip.height() - 600.0) < 1e-9);
}

aoc::core::PlacementContext placementContext() {
    aoc::core::PlacementContext context;
    context.monitorBoundsPx = {0, 0, 1920, 1080};
    context.policyBoundsPx = context.monitorBoundsPx;
    context.dpi = 96;
    context.clockSizeDip = {120.0, 40.0};
    context.edgeMarginDip = 24.0;
    context.allowedArea = {0.0, 0.0, 1.0, 1.0};
    context.excludedAreas = {{0.0, 0.0, 0.18, 0.18}};
    context.mode = aoc::core::MovementMode::WholeScreen;
    context.preferredCenter = aoc::core::NormalizedPoint{0.5, 0.5};
    context.randomSeed = 42;
    return context;
}

void testPlacement() {
    auto context = placementContext();
    const auto candidates = aoc::core::generateCandidates(context);
    CHECK(candidates.size() > 20);
    for (const auto& candidate : candidates) {
        CHECK(aoc::core::isValidPlacement(candidate, context));
        CHECK(candidate.left >= 24 && candidate.top >= 24);
    }
    const auto preferred = aoc::core::preferredPlacement(context);
    CHECK(preferred.has_value());
    aoc::core::ExposureMap exposure;
    exposure.seconds[0] = 10'000.0;
    context.previousRectPx = preferred;
    const auto choice = aoc::core::choosePlacement(context, exposure);
    CHECK(choice.has_value());
    CHECK(choice->boundsPx != *preferred);
    CHECK(choice->boundsPx.left > 24 || choice->boundsPx.top > 24);

    auto local = context;
    local.mode = aoc::core::MovementMode::LocalWander;
    local.excludedAreas.clear();
    local.preferredCenter = aoc::core::NormalizedPoint{0.5, 0.5};
    const auto localCandidates = aoc::core::generateCandidates(local);
    CHECK(!localCandidates.empty());
    for (const auto& candidate : localCandidates) {
        CHECK(std::abs(candidate.center().x - 960.0) < 1200.0);
        CHECK(std::abs(candidate.center().y - 540.0) < 800.0);
    }
}

void testMonitorAndFullscreen() {
    const auto first = aoc::core::MonitorInfo{"A", L"A", {0, 0, 1920, 1080}, {0, 0, 1920, 1040}, 96, 96, true, true};
    const auto second = aoc::core::MonitorInfo{"B", L"B", {1920, 0, 3840, 1080}, {1920, 0, 3840, 1040}, 144, 144, false, true};
    auto settings = aoc::core::Settings::defaults();
    settings.monitorMode = aoc::core::MonitorMode::Fixed;
    settings.fixedMonitorKey = "MISSING";
    const auto selection = aoc::core::selectMonitor({first, second}, settings);
    CHECK(selection.selected.has_value() && selection.selected->stableKey == "A");
    CHECK(selection.usedPrimaryFallback);

    const aoc::core::ForegroundWindowSnapshot positive{true, false, false, {0, 0, 1920, 1080}, {0, 0, 1920, 1080}, {0, 0, 1920, 1080}};
    CHECK(aoc::core::isFullscreenLike(positive));
    auto taskbar = positive;
    taskbar.shellWindow = true;
    CHECK(!aoc::core::isFullscreenLike(taskbar));
    auto normal = positive;
    normal.extendedFrameRectPx = {0, 0, 1920, 1040};
    CHECK(!aoc::core::isFullscreenLike(normal));
    auto tool = positive;
    tool.toolWindow = true;
    CHECK(!aoc::core::isFullscreenLike(tool));
    auto minimized = positive;
    minimized.minimized = true;
    CHECK(!aoc::core::isFullscreenLike(minimized));
}

void testSettings() {
    auto settings = aoc::core::Settings::defaults();
    settings.timeFormat = aoc::core::TimeFormat::TwentyFourHour;
    settings.showAmPm = false;
    settings.excludedAreas = {{0.1, 0.2, 0.3, 0.4}};
    settings.fixedMonitorKey = "DISPLAY\\Device=One";
    settings.opacity = 0.44;
    const auto roundTrip = aoc::core::deserializeSettings(aoc::core::serializeSettings(settings));
    CHECK(!roundTrip.recovered);
    CHECK(roundTrip.value.timeFormat == aoc::core::TimeFormat::TwentyFourHour);
    CHECK(roundTrip.value.fixedMonitorKey == settings.fixedMonitorKey);
    CHECK(std::abs(roundTrip.value.opacity - 0.44) < 1e-9);
    CHECK(roundTrip.value.excludedAreas.size() == 1);
    const auto migrated = aoc::core::deserializeSettings("fontSize=40\nopacity=0.2\n");
    CHECK(migrated.recovered);
    CHECK(std::abs(migrated.value.fontSizeDip - 40.0) < 1e-9);
    CHECK(aoc::core::deserializeSettings("this is not a settings file").recovered);
    auto invalid = settings;
    invalid.opacity = 4.0;
    invalid.allowedArea = {-1.0, -1.0, 3.0, 3.0};
    invalid.validateAndNormalize();
    CHECK(invalid.opacity == 1.0);
    CHECK(invalid.allowedArea.left == 0.0 && invalid.allowedArea.right == 1.0);
}

} // namespace

int main() {
    testClock();
    testExposure();
    testGeometry();
    testPlacement();
    testMonitorAndFullscreen();
    testSettings();
    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All Adaptive OLED Clock core tests passed\n";
    return EXIT_SUCCESS;
}

#include "aoc/core/clock.h"
#include "aoc/core/exposure.h"
#include "aoc/core/fullscreen.h"
#include "aoc/core/geometry.h"
#include "aoc/core/monitor.h"
#include "aoc/core/placement.h"
#include "aoc/core/settings.h"
#include "aoc/core/settings_change.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

int failures = 0;
int scenarioCount = 0;

void beginScenario(const char* name) {
    ++scenarioCount;
    std::cout << "[SCENARIO " << scenarioCount << "] " << name << '\n';
}

void check(bool condition, const char* expression) {
    if (!condition) {
        std::cerr << "FAIL: " << expression << '\n';
        ++failures;
    }
}

void checkNear(double actual, double expected, double tolerance, const char* expression) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << expression << " (actual=" << actual << ", expected=" << expected << ')'
                  << '\n';
        ++failures;
    }
}

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__)
#define CHECK_NEAR(actual, expected, tolerance) checkNear((actual), (expected), (tolerance), #actual " ~= " #expected)

constexpr double kTolerance = 1e-9;

void testClockFormattingAndBoundaries() {
    beginScenario("clock format selection, AM/PM, seconds, and locale pattern detection");
    CHECK(aoc::core::useTwelveHourClock(aoc::core::TimeFormat::TwelveHour,
                                        aoc::core::LocaleHourMode::TwentyFourHour));
    CHECK(!aoc::core::useTwelveHourClock(aoc::core::TimeFormat::TwentyFourHour,
                                         aoc::core::LocaleHourMode::TwelveHour));
    CHECK(aoc::core::useTwelveHourClock(aoc::core::TimeFormat::Locale,
                                        aoc::core::LocaleHourMode::TwelveHour));
    CHECK(!aoc::core::useTwelveHourClock(aoc::core::TimeFormat::Locale,
                                         aoc::core::LocaleHourMode::TwentyFourHour));

    std::tm time{};
    time.tm_hour = 0;
    time.tm_min = 7;
    time.tm_sec = 5;
    CHECK(aoc::core::formatClockText(time, aoc::core::TimeFormat::TwelveHour, true,
                                     aoc::core::LocaleHourMode::TwentyFourHour) == L"12:07 AM");
    CHECK(aoc::core::formatClockText(time, aoc::core::TimeFormat::TwelveHour, false,
                                     aoc::core::LocaleHourMode::TwentyFourHour) == L"12:07");
    CHECK(aoc::core::formatClockText(time, aoc::core::TimeFormat::TwelveHour, true,
                                     aoc::core::LocaleHourMode::TwentyFourHour, true) == L"12:07:05 AM");

    time.tm_hour = 12;
    CHECK(aoc::core::formatClockText(time, aoc::core::TimeFormat::TwelveHour, true,
                                     aoc::core::LocaleHourMode::TwentyFourHour) == L"12:07 PM");
    time.tm_hour = 13;
    time.tm_min = 58;
    CHECK(aoc::core::formatClockText(time, aoc::core::TimeFormat::TwelveHour, true,
                                     aoc::core::LocaleHourMode::TwentyFourHour) == L"1:58 PM");
    CHECK(aoc::core::formatClockText(time, aoc::core::TimeFormat::TwentyFourHour, true,
                                     aoc::core::LocaleHourMode::TwelveHour, true) == L"13:58:05");

    time.tm_hour = 4;
    time.tm_min = 2;
    time.tm_sec = 9;
    CHECK(aoc::core::formatClockText(time, aoc::core::TimeFormat::TwentyFourHour, false,
                                     aoc::core::LocaleHourMode::TwentyFourHour) == L"04:02");
    CHECK(aoc::core::localeHourModeFromPattern(L"h:mm tt") == aoc::core::LocaleHourMode::TwelveHour);
    CHECK(aoc::core::localeHourModeFromPattern(L"HH:mm") == aoc::core::LocaleHourMode::TwentyFourHour);

    beginScenario("clock minute and second timers land on the next exact boundary");
    using namespace std::chrono;
    const auto now = system_clock::time_point{} + hours(10) + minutes(15) + seconds(30) + milliseconds(999);
    const auto nextMinute = aoc::core::nextMinuteBoundary(now);
    const auto nextSecond = aoc::core::nextSecondBoundary(now);
    CHECK(duration_cast<milliseconds>(nextMinute.time_since_epoch()).count() ==
          duration_cast<milliseconds>((system_clock::time_point{} + hours(10) + minutes(16)).time_since_epoch()).count());
    CHECK(duration_cast<milliseconds>(nextSecond.time_since_epoch()).count() ==
          duration_cast<milliseconds>((system_clock::time_point{} + hours(10) + minutes(15) + seconds(31))
                                          .time_since_epoch())
              .count());
    const auto exactSecond = system_clock::time_point{} + seconds(30);
    CHECK(aoc::core::nextSecondBoundary(exactSecond) == system_clock::time_point{} + seconds(31));

    beginScenario("small shifts divide each major-movement cycle evenly, including subsecond offsets");
    CHECK(aoc::core::evenlySpacedShiftOffset(10, 3, 0) == milliseconds(2500));
    CHECK(aoc::core::evenlySpacedShiftOffset(10, 3, 1) == milliseconds(5000));
    CHECK(aoc::core::evenlySpacedShiftOffset(10, 3, 2) == milliseconds(7500));
    CHECK(aoc::core::evenlySpacedShiftOffset(1, 3, 0) == milliseconds(250));
    CHECK(aoc::core::evenlySpacedShiftOffset(1, 3, 2) == milliseconds(750));
    CHECK(aoc::core::evenlySpacedShiftOffset(10, 0, 0) == milliseconds::zero());
}

void testGeometryAndDpi() {
    beginScenario("normalized geometry, intersections, and DPI conversion are bounded");
    CHECK(aoc::core::clamp01(-0.25) == 0.0);
    CHECK(aoc::core::clamp01(1.25) == 1.0);
    CHECK(aoc::core::normalizeRect({0.8, 0.7, 0.2, 0.1}) == aoc::core::NormalizedRect{0.2, 0.1, 0.8, 0.7});
    CHECK(aoc::core::clampNormalizedRect({-1.0, 0.8, 2.0, -0.2}) == aoc::core::NormalizedRect{0.0, 0.0, 1.0, 0.8});

    const aoc::core::RectI monitor{100, 50, 2100, 1550};
    const auto physical = aoc::core::normalizedToPhysicalPixels({0.25, 0.20, 0.75, 0.80}, monitor);
    CHECK(physical == aoc::core::RectI{600, 350, 1600, 1250});
    const auto normalized = aoc::core::physicalToNormalized(physical, monitor);
    CHECK_NEAR(normalized.left, 0.25, kTolerance);
    CHECK_NEAR(normalized.top, 0.20, kTolerance);
    CHECK_NEAR(normalized.right, 0.75, kTolerance);
    CHECK_NEAR(normalized.bottom, 0.80, kTolerance);
    const auto outside = aoc::core::physicalToNormalized({-100, -100, 3000, 3000}, monitor);
    CHECK(outside == aoc::core::NormalizedRect{0.0, 0.0, 1.0, 1.0});

    CHECK_NEAR(aoc::core::dipToPixels(32.0, 144), 48.0, kTolerance);
    CHECK_NEAR(aoc::core::pixelsToDip(48.0, 144), 32.0, kTolerance);
    CHECK_NEAR(aoc::core::dipToPixels(32.0, 0), 32.0, kTolerance);
    const auto dip = aoc::core::pixelsToDip(aoc::core::RectI{0, 0, 1440, 900}, 144);
    CHECK_NEAR(dip.width(), 960.0, kTolerance);
    CHECK_NEAR(dip.height(), 600.0, kTolerance);
    CHECK(aoc::core::clampRectTo({-10, 10, 100, 100}, {0, 0, 80, 80}) == aoc::core::RectI{});
    CHECK(aoc::core::clampRectTo({-10, 10, 50, 50}, {0, 0, 80, 80}) == aoc::core::RectI{0, 10, 60, 50});
}

void testSettingsDefaultsMigrationAndRecovery() {
    beginScenario("settings defaults and OLED preset retain a slow edge-only movement policy");
    const auto defaults = aoc::core::Settings::defaults();
    CHECK(defaults.version == aoc::core::kCurrentSettingsVersion);
    CHECK_NEAR(defaults.opacity, 0.80, kTolerance);
    CHECK(defaults.textColor == aoc::core::Color{255, 255, 255, 255});
    CHECK_NEAR(defaults.boostOpacity, 1.0, kTolerance);
    CHECK(defaults.movementMode == aoc::core::MovementMode::EdgeOnly);
    CHECK(defaults.movementIntervalSeconds == 3600);
    CHECK(defaults.microShiftEnabled);
    CHECK(defaults.microShiftCount == 3);
    CHECK(defaults.microShiftDistancePx == 3);
    CHECK(defaults.localAreaRadiusPx == 100);
    CHECK(!defaults.localAreaAnchorSet);
    CHECK_NEAR(defaults.edgeMarginDip, 0.0, kTolerance);
    CHECK(defaults.showDate == false);
    CHECK(defaults.showSeconds == false);

    const auto safe = aoc::core::Settings::oledPreset();
    CHECK(safe.movementMode == aoc::core::MovementMode::EdgeOnly);
    CHECK_NEAR(safe.opacity, 0.50, kTolerance);
    CHECK(safe.textColor == aoc::core::Color{176, 176, 176, 255});
    CHECK_NEAR(safe.boostOpacity, 1.0, kTolerance);
    CHECK(safe.boostDurationSeconds <= defaults.boostDurationSeconds);
    CHECK(safe.movementIntervalSeconds == 1800);
    CHECK(safe.microShiftEnabled);
    CHECK(safe.microShiftCount == 4);
    CHECK(safe.microShiftDistancePx == 5);
    CHECK_NEAR(safe.edgeMarginDip, 0.0, kTolerance);

    beginScenario("schema-v6 minimal files preserve the 80 percent white Edge-only defaults");
    const auto defaultText = aoc::core::serializeSettings(defaults);
    CHECK(defaultText.find("version=6\n") != std::string::npos);
    const auto defaultRoundTrip = aoc::core::deserializeSettings(defaultText);
    CHECK(!defaultRoundTrip.recovered);
    CHECK(!defaultRoundTrip.migrated);
    CHECK_NEAR(defaultRoundTrip.value.opacity, 0.80, kTolerance);
    CHECK(defaultRoundTrip.value.movementMode == aoc::core::MovementMode::EdgeOnly);
    CHECK(defaultRoundTrip.value.allowedArea == aoc::core::NormalizedRect{0.0, 0.0, 1.0, 1.0});
    const auto minimalV6 = aoc::core::deserializeSettings("version=6\n");
    CHECK(!minimalV6.recovered);
    CHECK(!minimalV6.migrated);
    CHECK_NEAR(minimalV6.value.opacity, 0.80, kTolerance);
    CHECK(minimalV6.value.movementMode == aoc::core::MovementMode::EdgeOnly);
    CHECK(minimalV6.value.showSeconds == false && minimalV6.value.showDate == false);

    beginScenario("custom allowed areas clamp at the core boundary and persist exactly when valid");
    auto customArea = defaults;
    customArea.allowedArea = {0.001, 0.002, 0.999, 0.998};
    const auto customAreaRoundTrip = aoc::core::deserializeSettings(aoc::core::serializeSettings(customArea));
    CHECK(!customAreaRoundTrip.recovered);
    CHECK(customAreaRoundTrip.value.allowedArea == customArea.allowedArea);
    auto clampedArea = defaults;
    clampedArea.allowedArea = {-0.25, 0.75, 1.25, 1.5};
    clampedArea.validateAndNormalize();
    CHECK(clampedArea.allowedArea == aoc::core::NormalizedRect{0.0, 0.75, 1.0, 1.0});

    auto minimumDuration = defaults;
    minimumDuration.movementIntervalSeconds = 0;
    minimumDuration.validateAndNormalize();
    CHECK(minimumDuration.movementIntervalSeconds == 1);
    auto maximumDuration = defaults;
    maximumDuration.movementIntervalSeconds = 8 * 24 * 60 * 60;
    maximumDuration.validateAndNormalize();
    CHECK(maximumDuration.movementIntervalSeconds == 8 * 24 * 60 * 60);

    beginScenario("current settings schema round-trips display, placement, and customization fields");
    auto original = defaults;
    original.timeFormat = aoc::core::TimeFormat::TwentyFourHour;
    original.showAmPm = false;
    original.showSeconds = true;
    original.showDate = true;
    original.fontFamily = "QA%,;=,\nFont";
    original.fontWeight = aoc::core::FontWeight::SemiBold;
    original.fontSizeDip = 44.5;
    original.textColor = {1, 2, 3, 4};
    original.opacity = 0.44;
    original.movementIntervalSeconds = 17 * 60 + 23;
    original.movementMode = aoc::core::MovementMode::LocalWander;
    original.microShiftEnabled = false;
    original.microShiftCount = 7;
    original.microShiftDistancePx = 11;
    original.localAreaRadiusPx = 135;
    original.localAreaAnchor = {0.25, 0.75};
    original.localAreaAnchorSet = true;
    original.allowedArea = {0.1, 0.2, 0.8, 0.9};
    original.edgeMarginDip = 31.0;
    original.monitorMode = aoc::core::MonitorMode::Fixed;
    original.fixedMonitorKey = "DISPLAY%=\\Device=One;\n";
    original.hideInFullscreen = false;
    original.launchAtStartup = true;
    original.hotkeyEnabled = false;
    original.clockVisible = false;
    original.boostOpacity = 0.66;
    original.boostDurationSeconds = 19;
    const auto serialized = aoc::core::serializeSettings(original);
    CHECK(serialized.find("microShiftEnabled=0") != std::string::npos);
    CHECK(serialized.find("microShiftCount=7") != std::string::npos);
    CHECK(serialized.find("microShiftDistancePx=11") != std::string::npos);
    CHECK(serialized.find("localAreaRadiusPx=135") != std::string::npos);
    CHECK(serialized.find("localAreaAnchorSet=1") != std::string::npos);
    CHECK(serialized.find("excludedAreas") == std::string::npos);
    CHECK(serialized.find("preferredPosition") == std::string::npos);
    const auto roundTrip = aoc::core::deserializeSettings(serialized);
    CHECK(!roundTrip.recovered);
    CHECK(!roundTrip.migrated);
    CHECK(roundTrip.value.version == aoc::core::kCurrentSettingsVersion);
    CHECK(roundTrip.value.timeFormat == original.timeFormat);
    CHECK(roundTrip.value.showAmPm == original.showAmPm);
    CHECK(roundTrip.value.showSeconds == original.showSeconds);
    CHECK(roundTrip.value.showDate == original.showDate);
    CHECK(roundTrip.value.fontFamily == original.fontFamily);
    CHECK(roundTrip.value.fontWeight == original.fontWeight);
    CHECK_NEAR(roundTrip.value.fontSizeDip, original.fontSizeDip, kTolerance);
    CHECK(roundTrip.value.textColor == original.textColor);
    CHECK_NEAR(roundTrip.value.opacity, original.opacity, kTolerance);
    CHECK(roundTrip.value.movementIntervalSeconds == original.movementIntervalSeconds);
    CHECK(roundTrip.value.movementMode == original.movementMode);
    CHECK(roundTrip.value.microShiftEnabled == original.microShiftEnabled);
    CHECK(roundTrip.value.microShiftCount == original.microShiftCount);
    CHECK(roundTrip.value.microShiftDistancePx == original.microShiftDistancePx);
    CHECK(roundTrip.value.localAreaRadiusPx == original.localAreaRadiusPx);
    CHECK(roundTrip.value.localAreaAnchor == original.localAreaAnchor);
    CHECK(roundTrip.value.localAreaAnchorSet);
    CHECK(roundTrip.value.allowedArea == original.allowedArea);
    CHECK_NEAR(roundTrip.value.edgeMarginDip, original.edgeMarginDip, kTolerance);
    CHECK(roundTrip.value.monitorMode == original.monitorMode);
    CHECK(roundTrip.value.fixedMonitorKey == original.fixedMonitorKey);
    CHECK(roundTrip.value.hideInFullscreen == original.hideInFullscreen);
    CHECK(roundTrip.value.launchAtStartup == original.launchAtStartup);
    CHECK(roundTrip.value.hotkeyEnabled == original.hotkeyEnabled);
    CHECK(roundTrip.value.clockVisible == original.clockVisible);
    CHECK_NEAR(roundTrip.value.boostOpacity, original.boostOpacity, kTolerance);
    CHECK(roundTrip.value.boostDurationSeconds == original.boostDurationSeconds);

    beginScenario("schema v1 and legacy no-version settings migrate while preserving usable values");
    const auto v1 = aoc::core::deserializeSettings(
        "version=1\nfontSizeDip=40\nopacity=0.2\nshowSeconds=1\nshowDate=1\nmovementMode=2\n");
    CHECK(v1.recovered);
    CHECK(v1.migrated);
    CHECK(v1.value.version == aoc::core::kCurrentSettingsVersion);
    CHECK_NEAR(v1.value.fontSizeDip, 40.0, kTolerance);
    CHECK_NEAR(v1.value.opacity, 0.2, kTolerance);
    CHECK(v1.value.showSeconds && v1.value.showDate);
    CHECK(v1.value.movementMode == aoc::core::MovementMode::EdgeOnly);
    const auto legacy = aoc::core::deserializeSettings("fontSize=38\nopacity=0.3\n");
    CHECK(legacy.recovered && legacy.migrated);
    CHECK_NEAR(legacy.value.fontSizeDip, 38.0, kTolerance);
    const auto previousDefaults = aoc::core::deserializeSettings(
        "version=2\nmovementIntervalMinutes=5\nedgeMarginDip=24\n"
        "microShiftEnabled=1\nmicroShiftRadiusDip=8\npreferredPositionEnabled=1\n");
    CHECK(previousDefaults.recovered && previousDefaults.migrated);
    CHECK(previousDefaults.value.movementIntervalSeconds == 3600);
    CHECK(previousDefaults.value.microShiftEnabled);
    CHECK(previousDefaults.value.microShiftDistancePx == 8);
    CHECK_NEAR(previousDefaults.value.edgeMarginDip, 0.0, kTolerance);

    beginScenario("invalid settings values normalize and malformed files recover to safe defaults");
    auto invalid = defaults;
    invalid.version = 999;
    invalid.timeFormat = static_cast<aoc::core::TimeFormat>(99);
    invalid.fontWeight = static_cast<aoc::core::FontWeight>(99);
    invalid.movementMode = static_cast<aoc::core::MovementMode>(99);
    invalid.monitorMode = static_cast<aoc::core::MonitorMode>(99);
    invalid.fontSizeDip = std::numeric_limits<double>::quiet_NaN();
    invalid.opacity = std::numeric_limits<double>::infinity();
    invalid.boostOpacity = -5.0;
    invalid.edgeMarginDip = -5.0;
    invalid.movementIntervalSeconds = 999;
    invalid.microShiftCount = 999;
    invalid.microShiftDistancePx = 999;
    invalid.localAreaRadiusPx = 99999;
    invalid.localAreaAnchor = {std::numeric_limits<double>::quiet_NaN(), 4.0};
    invalid.localAreaAnchorSet = true;
    invalid.boostDurationSeconds = 0;
    invalid.allowedArea = {-1.0, 2.0, 3.0, -2.0};
    invalid.fontFamily.clear();
    invalid.fixedMonitorKey.assign(600, 'k');
    invalid.validateAndNormalize();
    CHECK(invalid.version == aoc::core::kCurrentSettingsVersion);
    CHECK(invalid.timeFormat == aoc::core::TimeFormat::Locale);
    CHECK(invalid.fontWeight == aoc::core::FontWeight::Normal);
    CHECK(invalid.movementMode == aoc::core::MovementMode::EdgeOnly);
    CHECK(invalid.monitorMode == aoc::core::MonitorMode::FollowPrimary);
    CHECK_NEAR(invalid.fontSizeDip, 32.0, kTolerance);
    CHECK_NEAR(invalid.opacity, 0.80, kTolerance);
    CHECK_NEAR(invalid.boostOpacity, 0.0, kTolerance);
    CHECK_NEAR(invalid.edgeMarginDip, 0.0, kTolerance);
    CHECK(invalid.movementIntervalSeconds == 999);
    CHECK(invalid.microShiftCount == 100);
    CHECK(invalid.microShiftDistancePx == 100);
    CHECK(invalid.localAreaRadiusPx == 10000);
    CHECK(invalid.localAreaAnchor == aoc::core::NormalizedPoint{0.5, 0.5});
    CHECK(!invalid.localAreaAnchorSet);
    CHECK(invalid.boostDurationSeconds == 1);
    CHECK(invalid.allowedArea == aoc::core::NormalizedRect{0.0, 0.0, 1.0, 1.0});
    CHECK(invalid.fontFamily == "Segoe UI");
    CHECK(invalid.fixedMonitorKey.size() == 512);

    const auto corrupt = aoc::core::deserializeSettings(
        "version=2\nopacity=nan\nshowSeconds=maybe\nallowedArea=broken\nexcludedAreas=0,0,0.2,0.2;broken\n");
    CHECK(corrupt.recovered);
    CHECK(!corrupt.value.showSeconds);
    CHECK_NEAR(corrupt.value.opacity, 0.80, kTolerance);
    CHECK(corrupt.value.allowedArea == aoc::core::Settings::defaults().allowedArea);
    CHECK(aoc::core::deserializeSettings("version=7\nopacity=0.1\n").recovered);
    CHECK(aoc::core::deserializeSettings("version=not-a-number\n").recovered);
    CHECK(aoc::core::deserializeSettings("this is not a settings file").recovered);
}

aoc::core::PlacementContext placementContext() {
    aoc::core::PlacementContext context;
    context.monitorBoundsPx = {100, 50, 2100, 1550};
    context.policyBoundsPx = context.monitorBoundsPx;
    context.dpi = 96;
    context.clockSizeDip = {120.0, 40.0};
    context.edgeMarginDip = 0.0;
    context.allowedArea = {0.0, 0.0, 1.0, 1.0};
    context.mode = aoc::core::MovementMode::EdgeOnly;
    context.randomSeed = 42;
    return context;
}

aoc::core::RectI movementBoundsForTest(const aoc::core::PlacementContext& context) {
    const int margin = static_cast<int>(std::lround(aoc::core::dipToPixels(context.edgeMarginDip, context.dpi)));
    const aoc::core::RectI marginBounds{context.policyBoundsPx.left + margin,
                                        context.policyBoundsPx.top + margin,
                                        context.policyBoundsPx.right - margin,
                                        context.policyBoundsPx.bottom - margin};
    return marginBounds.intersection(
        aoc::core::normalizedToPhysicalPixels(context.allowedArea, context.monitorBoundsPx));
}

aoc::core::SizeD clockSizePixelsForTest(const aoc::core::PlacementContext& context) {
    return {static_cast<double>(std::lround(aoc::core::dipToPixels(context.clockSizeDip.width, context.dpi))),
            static_cast<double>(std::lround(aoc::core::dipToPixels(context.clockSizeDip.height, context.dpi)))};
}

aoc::core::RectI centeredRectForTest(aoc::core::PointD center, aoc::core::SizeD size) {
    const int width = static_cast<int>(std::lround(size.width));
    const int height = static_cast<int>(std::lround(size.height));
    const int left = static_cast<int>(std::lround(center.x - width / 2.0));
    const int top = static_cast<int>(std::lround(center.y - height / 2.0));
    return {left, top, left + width, top + height};
}

bool touchesBoundary(const aoc::core::RectI& candidate, const aoc::core::RectI& bounds) {
    return candidate.left == bounds.left || candidate.top == bounds.top ||
           candidate.right == bounds.right || candidate.bottom == bounds.bottom;
}

bool intersects(const aoc::core::RectI& first, const aoc::core::RectI& second) {
    return first.intersects(second);
}

void testPlacementModesAndConstraints() {
    beginScenario("edge-only candidates stay on the perimeter with no interior anchors");
    auto edge = placementContext();
    const auto edgeBounds = movementBoundsForTest(edge);
    const auto edgeCandidates = aoc::core::generateCandidates(edge);
    CHECK(edgeCandidates.size() > 20);
    for (const auto& candidate : edgeCandidates) {
        CHECK(aoc::core::isValidPlacement(candidate, edge));
        CHECK(candidate.width() == 120 && candidate.height() == 40);
        CHECK(touchesBoundary(candidate, edgeBounds));
    }

    beginScenario("four-corners, whole-screen, and local-area modes expose their intended candidate regions");
    auto corners = edge;
    corners.mode = aoc::core::MovementMode::FourCorners;
    const auto cornerCandidates = aoc::core::generateCandidates(corners);
    CHECK(cornerCandidates.size() == 4);
    for (const auto& candidate : cornerCandidates) CHECK(touchesBoundary(candidate, edgeBounds));

    auto whole = edge;
    whole.mode = aoc::core::MovementMode::WholeScreen;
    const auto wholeCandidates = aoc::core::generateCandidates(whole);
    const auto wholeCenter = centeredRectForTest(edgeBounds.center(), clockSizePixelsForTest(whole));
    CHECK(wholeCandidates.size() > edgeCandidates.size());
    CHECK(std::find(wholeCandidates.begin(), wholeCandidates.end(), wholeCenter) != wholeCandidates.end());
    bool hasInteriorWholeCandidate = false;
    for (const auto& candidate : wholeCandidates) {
        if (!touchesBoundary(candidate, edgeBounds)) hasInteriorWholeCandidate = true;
    }
    CHECK(hasInteriorWholeCandidate);

    auto local = edge;
    local.mode = aoc::core::MovementMode::LocalWander;
    local.localAnchorPx = aoc::core::PointD{650.0, 500.0};
    local.localRadiusPx = 100;
    const auto localCandidates = aoc::core::generateCandidates(local);
    const auto preferredRect = centeredRectForTest(*local.localAnchorPx, clockSizePixelsForTest(local));
    CHECK(localCandidates.size() > 1);
    CHECK(std::find(localCandidates.begin(), localCandidates.end(), preferredRect) != localCandidates.end());
    for (const auto& candidate : localCandidates) {
        CHECK(aoc::core::isValidPlacement(candidate, local));
        CHECK(std::abs(candidate.center().x - local.localAnchorPx->x) <= local.localRadiusPx + 1.0);
        CHECK(std::abs(candidate.center().y - local.localAnchorPx->y) <= local.localRadiusPx + 1.0);
    }

    beginScenario("accelerated 240-move simulation keeps every mode inside its intended geometry");
    auto simulate = [](aoc::core::PlacementContext context, int moves) {
        std::vector<aoc::core::RectI> visited;
        aoc::core::ExposureMap exposure;
        for (int step = 0; step < moves; ++step) {
            context.randomSeed = 1000 + static_cast<std::uint64_t>(step);
            const auto choice = aoc::core::choosePlacement(context, exposure);
            if (!choice.has_value()) break;
            visited.push_back(choice->boundsPx);
            exposure.charge(aoc::core::physicalToNormalized(choice->boundsPx, context.monitorBoundsPx), 1.0);
            context.previousRectPx = choice->boundsPx;
            context.recentMacroRects.insert(context.recentMacroRects.begin(), choice->boundsPx);
            if (context.recentMacroRects.size() > 12) context.recentMacroRects.resize(12);
        }
        return visited;
    };

    const auto simulatedEdge = simulate(edge, 240);
    CHECK(simulatedEdge.size() == 240);
    for (const auto& rect : simulatedEdge) CHECK(touchesBoundary(rect, edgeBounds));

    const auto simulatedCorners = simulate(corners, 80);
    CHECK(simulatedCorners.size() == 80);
    for (const auto& rect : simulatedCorners) {
        CHECK(std::find(cornerCandidates.begin(), cornerCandidates.end(), rect) != cornerCandidates.end());
    }

    const auto simulatedWhole = simulate(whole, 240);
    CHECK(simulatedWhole.size() == 240);
    int wholeInteriorMoves = 0;
    std::vector<aoc::core::RectI> uniqueWhole;
    for (const auto& rect : simulatedWhole) {
        if (!touchesBoundary(rect, edgeBounds)) ++wholeInteriorMoves;
        if (std::find(uniqueWhole.begin(), uniqueWhole.end(), rect) == uniqueWhole.end()) uniqueWhole.push_back(rect);
    }
    CHECK(wholeInteriorMoves > 120);
    CHECK(uniqueWhole.size() > 40);

    const auto simulatedLocal = simulate(local, 240);
    CHECK(simulatedLocal.size() == 240);
    for (const auto& rect : simulatedLocal) {
        CHECK(std::abs(rect.center().x - local.localAnchorPx->x) <= local.localRadiusPx + 1.0);
        CHECK(std::abs(rect.center().y - local.localAnchorPx->y) <= local.localRadiusPx + 1.0);
    }

    auto localAtBorder = local;
    localAtBorder.localAnchorPx = aoc::core::PointD{edgeBounds.right - 10.0, edgeBounds.bottom - 10.0};
    for (const auto& rect : aoc::core::generateCandidates(localAtBorder)) {
        CHECK(aoc::core::isValidPlacement(rect, localAtBorder));
        CHECK(rect.right <= edgeBounds.right && rect.bottom <= edgeBounds.bottom);
    }

    beginScenario("DPI-scaled margins, allowed bounds, and padded surfaces are enforced");
    auto dpi = placementContext();
    dpi.monitorBoundsPx = {0, 0, 2560, 1440};
    dpi.policyBoundsPx = dpi.monitorBoundsPx;
    dpi.dpi = 150;
    dpi.clockSizeDip = {100.0, 40.0};
    dpi.edgeMarginDip = 24.0;
    const auto dpiCandidates = aoc::core::generateCandidates(dpi);
    const int expectedMargin = static_cast<int>(std::lround(aoc::core::dipToPixels(24.0, 150)));
    CHECK(expectedMargin == 38);
    for (const auto& candidate : dpiCandidates) {
        CHECK(candidate.width() == 156 && candidate.height() == 63);
        CHECK(candidate.left >= expectedMargin && candidate.top >= expectedMargin);
        CHECK(candidate.right <= 2560 - expectedMargin && candidate.bottom <= 1440 - expectedMargin);
    }
    const aoc::core::RectI card{100, 200, 300, 260};
    CHECK(aoc::core::paddedPlacementRect(card, 8.0, 150) == aoc::core::RectI{87, 187, 313, 273});

    auto constrained = placementContext();
    constrained.mode = aoc::core::MovementMode::WholeScreen;
    constrained.allowedArea = {0.20, 0.10, 0.90, 0.90};
    constrained.surfacePaddingDip = 4.0;
    const auto constrainedCandidates = aoc::core::generateCandidates(constrained);
    CHECK(!constrainedCandidates.empty());
    for (const auto& candidate : constrainedCandidates) {
        const auto surface = aoc::core::paddedPlacementRect(candidate,
                                                              constrained.surfacePaddingDip,
                                                              constrained.dpi);
        CHECK(aoc::core::isValidPlacement(candidate, constrained));
        CHECK(movementBoundsForTest(constrained).contains(surface));
    }

    beginScenario("routine text resizing stays near the current anchor instead of selecting a new position");
    auto resize = placementContext();
    const aoc::core::RectI current{resize.policyBoundsPx.right - 120, 300,
                                  resize.policyBoundsPx.right, 340};
    const aoc::core::RectI wider{current.left, current.top, current.left + 150, current.bottom};
    const auto fitted = aoc::core::fitPlacementNear(wider, resize);
    CHECK(fitted.isValid());
    CHECK(fitted.right == resize.policyBoundsPx.right);
    CHECK(fitted.top == current.top);
    CHECK(fitted.width() == 150);
}

void testPlacementHistory() {
    beginScenario("placement history avoids exact repeats and overlapping recent macro anchors");
    auto context = placementContext();
    const auto candidates = aoc::core::generateCandidates(context);
    CHECK(candidates.size() > 3);
    context.previousRectPx = candidates.front();
    context.recentMacroRects = {candidates[1]};
    const auto choice = aoc::core::choosePlacement(context, aoc::core::ExposureMap{});
    CHECK(choice.has_value());
    if (choice.has_value()) {
        CHECK(aoc::core::isValidPlacement(choice->boundsPx, context));
        CHECK(choice->boundsPx != candidates.front());
        CHECK(choice->boundsPx != candidates[1]);
        CHECK(!intersects(choice->boundsPx, candidates.front()));
        CHECK(!intersects(choice->boundsPx, candidates[1]));
    }
}

void testExposureMathTrackerAndPersistence() {
    beginScenario("exposure intersection weighting conserves visible charge and reports statistics");
    aoc::core::ExposureMap charged;
    const aoc::core::NormalizedRect quarter{0.0, 0.0, 0.25, 0.25};
    charged.charge(quarter, 20.0);
    CHECK_NEAR(charged.totalSeconds(), 20.0, kTolerance);
    CHECK_NEAR(charged.weightedExposure(quarter), 20.0, kTolerance);
    CHECK_NEAR(charged.weightedExposure(aoc::core::NormalizedRect{0.0, 0.0, 1.0, 1.0}), 20.0, kTolerance);

    aoc::core::ExposureMap weighted;
    weighted.seconds.fill(0.0);
    weighted.seconds[0] = 10.0;
    const aoc::core::NormalizedRect firstCell{0.0, 0.0,
                                               1.0 / aoc::core::kExposureColumns,
                                               1.0 / aoc::core::kExposureRows};
    CHECK_NEAR(weighted.weightedExposure(firstCell), 10.0, kTolerance);
    CHECK_NEAR(weighted.weightedExposure(aoc::core::NormalizedRect{
                   0.0, 0.0, 0.5 / aoc::core::kExposureColumns, 0.5 / aoc::core::kExposureRows}),
               2.5, kTolerance);
    const double beforeInvalid = charged.totalSeconds();
    charged.charge({0.0, 0.0, 0.0, 0.0}, 10.0);
    charged.charge({0.0, 0.0, 1.0, 1.0}, -1.0);
    charged.charge({0.0, 0.0, 1.0, 1.0}, std::numeric_limits<double>::quiet_NaN());
    CHECK_NEAR(charged.totalSeconds(), beforeInvalid, kTolerance);

    beginScenario("exposure min/max/total/imbalance and reset have stable edge-case behavior");
    aoc::core::ExposureMap stats;
    stats.seconds.fill(2.0);
    stats.seconds[0] = 10.0;
    CHECK_NEAR(stats.totalSeconds(), 2.0 * aoc::core::kExposureCellCount + 8.0, kTolerance);
    CHECK(stats.leastExposedCell() == std::pair<std::size_t, std::size_t>{1, 0});
    CHECK(stats.mostExposedCell() == std::pair<std::size_t, std::size_t>{0, 0});
    CHECK_NEAR(stats.imbalance(), 0.8, kTolerance);
    CHECK(stats.cell(aoc::core::kExposureColumns, 0) == 0.0);
    stats.reset();
    CHECK_NEAR(stats.totalSeconds(), 0.0, kTolerance);
    CHECK_NEAR(stats.imbalance(), 0.0, kTolerance);
    CHECK(stats.leastExposedCell() == std::pair<std::size_t, std::size_t>{0, 0});

    beginScenario("visible-only exposure tracking keeps monitor histories independent");
    aoc::core::ExposureStore store;
    aoc::core::ExposureTracker tracker(store);
    const auto start = std::chrono::steady_clock::time_point{};
    tracker.setState("MONITOR-A", quarter, true, start);
    tracker.checkpoint(start + std::chrono::seconds(30));
    tracker.setState("MONITOR-A", quarter, false, start + std::chrono::seconds(30));
    tracker.checkpoint(start + std::chrono::seconds(90));
    CHECK_NEAR(store.forMonitor("MONITOR-A").totalSeconds(), 30.0, kTolerance);
    tracker.setState("MONITOR-B", {0.5, 0.5, 0.75, 0.75}, true, start + std::chrono::seconds(90));
    tracker.settle(start + std::chrono::seconds(100));
    CHECK(!tracker.active());
    CHECK_NEAR(store.forMonitor("MONITOR-A").totalSeconds(), 30.0, kTolerance);
    CHECK_NEAR(store.forMonitor("MONITOR-B").totalSeconds(), 10.0, kTolerance);
    tracker.checkpoint(start + std::chrono::seconds(200));
    CHECK_NEAR(store.forMonitor("MONITOR-B").totalSeconds(), 10.0, kTolerance);

    beginScenario("exposure snapshot persistence round-trips special monitor keys and rejects corruption");
    aoc::core::ExposureStore persisted;
    const std::string specialKey = "MONITOR%=A\nB";
    persisted.forMonitor(specialKey).seconds[0] = 12.5;
    persisted.forMonitor("MONITOR-B").seconds[17] = 4.25;
    const auto exposureText = aoc::core::serializeExposure(persisted);
    const auto restored = aoc::core::deserializeExposure(exposureText);
    CHECK(!restored.recovered);
    CHECK(restored.value.maps().size() == 2);
    CHECK(restored.value.find(specialKey) != nullptr);
    CHECK_NEAR(restored.value.find(specialKey)->seconds[0], 12.5, kTolerance);
    CHECK_NEAR(restored.value.find("MONITOR-B")->seconds[17], 4.25, kTolerance);
    CHECK(aoc::core::deserializeExposure("version=1\nmonitorCount=1\nmonitor.0.key=broken\n").recovered);
    CHECK(aoc::core::deserializeExposure("version=2\nmonitorCount=0\n").recovered);
    CHECK(aoc::core::deserializeExposure("version=1\nmonitorCount=1\nmonitor.0.key=A\nmonitor.0.cells=1,2\n")
              .recovered);
    persisted.clear();
    CHECK(persisted.maps().empty());
}

void testMonitorFallbackAndFullscreenHeuristics() {
    beginScenario("monitor selection handles fixed monitor success, missing fallback, and empty inventory");
    const aoc::core::MonitorInfo primary{"PRIMARY", L"Primary", {0, 0, 1920, 1080},
                                         {0, 0, 1920, 1040}, 96, 96, true, true};
    const aoc::core::MonitorInfo secondary{"SECONDARY", L"Secondary", {1920, 0, 3840, 1080},
                                           {1920, 0, 3840, 1040}, 144, 144, false, true};
    const std::vector<aoc::core::MonitorInfo> monitors{primary, secondary};
    auto settings = aoc::core::Settings::defaults();
    settings.monitorMode = aoc::core::MonitorMode::Fixed;
    settings.fixedMonitorKey = "SECONDARY";
    const auto fixed = aoc::core::selectMonitor(monitors, settings);
    CHECK(fixed.selected.has_value() && fixed.selected->stableKey == "SECONDARY");
    CHECK(!fixed.usedPrimaryFallback);
    settings.fixedMonitorKey = "MISSING";
    const auto fallback = aoc::core::selectMonitor(monitors, settings);
    CHECK(fallback.selected.has_value() && fallback.selected->stableKey == "PRIMARY");
    CHECK(fallback.usedPrimaryFallback);
    const auto noPrimary = aoc::core::selectMonitor(
        std::vector<aoc::core::MonitorInfo>{secondary}, aoc::core::Settings::defaults());
    CHECK(noPrimary.selected.has_value() && noPrimary.selected->stableKey == "SECONDARY");
    CHECK(!aoc::core::selectMonitor({}, settings).selected.has_value());

    beginScenario("fullscreen heuristic covers positive, tolerance, normal-window, and policy negatives");
    const aoc::core::ForegroundWindowSnapshot positive{true, false, false,
                                                       {0, 0, 1920, 1080}, {0, 0, 1920, 1080},
                                                       {0, 0, 1920, 1080}, false};
    CHECK(aoc::core::isFullscreenLike(positive));
    auto tolerated = positive;
    tolerated.extendedFrameRectPx = {2, 1, 1918, 1079};
    CHECK(aoc::core::isFullscreenLike(tolerated));
    auto outsideTolerance = positive;
    outsideTolerance.extendedFrameRectPx = {4, 0, 1920, 1080};
    CHECK(!aoc::core::isFullscreenLike(outsideTolerance));
    auto noExtendedFrame = positive;
    noExtendedFrame.extendedFrameRectPx = {};
    CHECK(aoc::core::isFullscreenLike(noExtendedFrame));
    auto taskbar = positive;
    taskbar.shellWindow = true;
    CHECK(!aoc::core::isFullscreenLike(taskbar));
    auto tool = positive;
    tool.toolWindow = true;
    CHECK(!aoc::core::isFullscreenLike(tool));
    auto hidden = positive;
    hidden.visible = false;
    CHECK(!aoc::core::isFullscreenLike(hidden));
    auto minimized = positive;
    minimized.minimized = true;
    CHECK(!aoc::core::isFullscreenLike(minimized));
    auto normal = positive;
    normal.extendedFrameRectPx = {0, 0, 1920, 1040};
    CHECK(!aoc::core::isFullscreenLike(normal));
    auto invalidMonitor = positive;
    invalidMonitor.monitorBoundsPx = {};
    CHECK(!aoc::core::isFullscreenLike(invalidMonitor));
}

void testSettingsChangeClassification() {
    beginScenario("settings changes map to the required runtime side effects");
    const auto defaults = aoc::core::Settings::defaults();
    const auto unchanged = aoc::core::classifySettingsChange(defaults, defaults);
    CHECK(!unchanged.anyChanged);

    auto appearance = defaults;
    appearance.showSeconds = !appearance.showSeconds;
    const auto appearanceChange = aoc::core::classifySettingsChange(defaults, appearance);
    CHECK(appearanceChange.anyChanged);
    CHECK(appearanceChange.appearanceChanged);
    CHECK(appearanceChange.secondsChanged);
    CHECK(!appearanceChange.placementPolicyChanged);

    auto placement = defaults;
    placement.movementMode = aoc::core::MovementMode::WholeScreen;
    const auto placementChange = aoc::core::classifySettingsChange(defaults, placement);
    CHECK(placementChange.anyChanged);
    CHECK(placementChange.movementModeChanged);
    CHECK(placementChange.placementPolicyChanged);
    CHECK(!placementChange.appearanceChanged);

    auto monitor = defaults;
    monitor.monitorMode = aoc::core::MonitorMode::Fixed;
    monitor.fixedMonitorKey = "display-2";
    CHECK(aoc::core::classifySettingsChange(defaults, monitor).monitorSelectionChanged);

    auto integrations = defaults;
    integrations.launchAtStartup = !integrations.launchAtStartup;
    integrations.hotkeyEnabled = !integrations.hotkeyEnabled;
    integrations.movementIntervalSeconds += 1;
    const auto integrationChange = aoc::core::classifySettingsChange(defaults, integrations);
    CHECK(integrationChange.startupChanged);
    CHECK(integrationChange.hotkeyChanged);
    CHECK(integrationChange.movementIntervalChanged);

    auto micro = defaults;
    micro.microShiftDistancePx += 1;
    micro.microShiftCount += 1;
    const auto microChange = aoc::core::classifySettingsChange(defaults, micro);
    CHECK(microChange.anyChanged);
    CHECK(microChange.microShiftChanged);
    CHECK(!microChange.placementPolicyChanged);

}

} // namespace

int main() {
    testClockFormattingAndBoundaries();
    testGeometryAndDpi();
    testSettingsDefaultsMigrationAndRecovery();
    testPlacementModesAndConstraints();
    testPlacementHistory();
    testExposureMathTrackerAndPersistence();
    testMonitorFallbackAndFullscreenHeuristics();
    testSettingsChangeClassification();

    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed across " << scenarioCount << " deterministic scenarios\n";
        return EXIT_FAILURE;
    }
    std::cout << "All Adaptive OLED Clock private QA assertions passed: " << scenarioCount
              << " deterministic scenarios\n";
    return EXIT_SUCCESS;
}

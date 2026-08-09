#include "aoc/core/settings.h"

#include "aoc/core/geometry.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <map>
#include <sstream>
#include <string_view>

namespace aoc::core {
namespace {

std::string escapeValue(std::string_view value) {
    std::ostringstream output;
    output << std::uppercase << std::hex;
    for (const unsigned char character : value) {
        if (character == '%' || character == ';' || character == '=' || character == ',' ||
            character == '\n' || character == '\r') {
            output << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(character);
        } else {
            output << static_cast<char>(character);
        }
    }
    return output.str();
}

std::string unescapeValue(std::string_view value) {
    std::string output;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '%' && index + 2 < value.size()) {
            try {
                output.push_back(static_cast<char>(std::stoi(std::string(value.substr(index + 1, 2)), nullptr, 16)));
                index += 2;
                continue;
            } catch (...) {
                // Preserve malformed escapes so a recoverable file is never silently rewritten as empty text.
            }
        }
        output.push_back(value[index]);
    }
    return output;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
}

bool parseDouble(std::string_view text, double& value) {
    try {
        std::size_t consumed = 0;
        const std::string copy(text);
        value = std::stod(copy, &consumed);
        return consumed == copy.size() && std::isfinite(value);
    } catch (...) {
        return false;
    }
}

bool parseInt(std::string_view text, int& value) {
    try {
        std::size_t consumed = 0;
        const std::string copy(text);
        value = std::stoi(copy, &consumed);
        return consumed == copy.size();
    } catch (...) {
        return false;
    }
}

bool parseBool(std::string_view text, bool& value) {
    if (text == "1" || text == "true" || text == "TRUE") {
        value = true;
        return true;
    }
    if (text == "0" || text == "false" || text == "FALSE") {
        value = false;
        return true;
    }
    return false;
}

bool parseRect(std::string_view text, NormalizedRect& rect) {
    std::stringstream stream{std::string(text)};
    char comma = 0;
    if (!(stream >> rect.left >> comma) || comma != ',') return false;
    if (!(stream >> rect.top >> comma) || comma != ',') return false;
    if (!(stream >> rect.right >> comma) || comma != ',') return false;
    if (!(stream >> rect.bottom) || !rect.isValid()) return false;
    rect = clampNormalizedRect(rect);
    return rect.isValid();
}

bool parsePoint(std::string_view text, NormalizedPoint& point) {
    std::stringstream stream{std::string(text)};
    char comma = 0;
    if (!(stream >> point.x >> comma >> point.y) || comma != ',' ||
        !std::isfinite(point.x) || !std::isfinite(point.y)) {
        return false;
    }
    return true;
}

std::string rectText(const NormalizedRect& rect) {
    std::ostringstream stream;
    stream << std::setprecision(17) << rect.left << ',' << rect.top << ',' << rect.right << ',' << rect.bottom;
    return stream.str();
}

bool parseColor(std::string_view text, Color& color) {
    std::stringstream stream{std::string(text)};
    char comma = 0;
    int channels[4]{};
    for (int index = 0; index < 4; ++index) {
        if (!(stream >> channels[index]) || channels[index] < 0 || channels[index] > 255) return false;
        if (index != 3 && (!(stream >> comma) || comma != ',')) return false;
    }
    color = {static_cast<std::uint8_t>(channels[0]), static_cast<std::uint8_t>(channels[1]),
             static_cast<std::uint8_t>(channels[2]), static_cast<std::uint8_t>(channels[3])};
    return true;
}

std::map<std::string, std::string> parseProperties(const std::string& text, bool& syntaxOk) {
    std::map<std::string, std::string> properties;
    syntaxOk = true;
    std::stringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const auto separator = line.find('=');
        if (separator == std::string::npos || separator == 0) {
            syntaxOk = false;
            continue;
        }
        properties[trim(line.substr(0, separator))] = line.substr(separator + 1);
    }
    return properties;
}

template <typename T, typename Parser>
void readOptional(const std::map<std::string, std::string>& properties,
                  const char* key,
                  T& destination,
                  bool& invalid,
                  Parser parser) {
    const auto found = properties.find(key);
    if (found == properties.end()) return;
    T parsed{};
    if (parser(found->second, parsed)) {
        destination = parsed;
    } else {
        invalid = true;
    }
}

template <typename T>
void readNumber(const std::map<std::string, std::string>& properties,
                const char* key,
                T& destination,
                bool& invalid) {
    readOptional<T>(properties, key, destination, invalid, [](std::string_view text, T& value) {
        if constexpr (std::is_same_v<T, double>) {
            return parseDouble(text, value);
        } else {
            int parsed = 0;
            if (!parseInt(text, parsed)) return false;
            value = static_cast<T>(parsed);
            return true;
        }
    });
}

void readBool(const std::map<std::string, std::string>& properties,
              const char* key,
              bool& destination,
              bool& invalid) {
    readOptional<bool>(properties, key, destination, invalid,
                       [](std::string_view text, bool& value) { return parseBool(text, value); });
}

} // namespace

Settings Settings::defaults() {
    Settings settings;
    settings.version = kCurrentSettingsVersion;
    settings.timeFormat = TimeFormat::Locale;
    settings.showAmPm = true;
    settings.showSeconds = false;
    settings.showDate = false;
    settings.fontFamily = "Segoe UI";
    settings.fontWeight = FontWeight::Normal;
    settings.fontSizeDip = 32.0;
    settings.textColor = {176, 176, 176, 255};
    settings.opacity = 0.45;
    settings.movementIntervalMinutes = 5;
    settings.movementMode = MovementMode::EdgeOnly;
    settings.microShiftEnabled = true;
    settings.microShiftRadiusDip = 8.0;
    settings.allowedArea = {0.0, 0.0, 1.0, 1.0};
    settings.excludedAreas.clear();
    settings.edgeMarginDip = 24.0;
    settings.monitorMode = MonitorMode::FollowPrimary;
    settings.fixedMonitorKey.clear();
    settings.preferredPosition = {0.5, 0.5};
    settings.preferredPositionEnabled = false;
    settings.hideInFullscreen = true;
    settings.launchAtStartup = false;
    settings.hotkeyEnabled = true;
    settings.clockVisible = true;
    settings.boostOpacity = 0.85;
    settings.boostDurationSeconds = 15;
    return settings;
}

Settings Settings::oledSafePreset() {
    Settings settings = defaults();
    settings.opacity = 0.31;
    settings.boostOpacity = 0.70;
    settings.boostDurationSeconds = 10;
    settings.movementIntervalMinutes = 5;
    settings.microShiftRadiusDip = 6.0;
    settings.edgeMarginDip = 32.0;
    return settings;
}

void Settings::validateAndNormalize() noexcept {
    version = kCurrentSettingsVersion;
    if (static_cast<int>(timeFormat) < static_cast<int>(TimeFormat::Locale) ||
        static_cast<int>(timeFormat) > static_cast<int>(TimeFormat::TwentyFourHour)) {
        timeFormat = TimeFormat::Locale;
    }
    if (static_cast<int>(fontWeight) < static_cast<int>(FontWeight::Normal) ||
        static_cast<int>(fontWeight) > static_cast<int>(FontWeight::SemiBold)) {
        fontWeight = FontWeight::Normal;
    }
    if (static_cast<int>(movementMode) < static_cast<int>(MovementMode::WholeScreen) ||
        static_cast<int>(movementMode) > static_cast<int>(MovementMode::EdgeOnly)) {
        movementMode = MovementMode::EdgeOnly;
    }
    if (static_cast<int>(monitorMode) < static_cast<int>(MonitorMode::FollowPrimary) ||
        static_cast<int>(monitorMode) > static_cast<int>(MonitorMode::Fixed)) {
        monitorMode = MonitorMode::FollowPrimary;
    }
    if (!std::isfinite(fontSizeDip)) fontSizeDip = 32.0;
    if (!std::isfinite(opacity)) opacity = 0.45;
    if (!std::isfinite(boostOpacity)) boostOpacity = 0.85;
    if (!std::isfinite(microShiftRadiusDip)) microShiftRadiusDip = 8.0;
    if (!std::isfinite(edgeMarginDip)) edgeMarginDip = 24.0;
    fontSizeDip = std::clamp(fontSizeDip, 8.0, 128.0);
    opacity = std::clamp(opacity, 0.0, 1.0);
    boostOpacity = std::clamp(boostOpacity, 0.0, 1.0);
    microShiftRadiusDip = std::clamp(microShiftRadiusDip, 0.0, 64.0);
    edgeMarginDip = std::clamp(edgeMarginDip, 0.0, 500.0);
    movementIntervalMinutes = std::clamp(movementIntervalMinutes, 1, 120);
    boostDurationSeconds = std::clamp(boostDurationSeconds, 1, 300);
    allowedArea = clampNormalizedRect(allowedArea);
    if (!allowedArea.isValid()) allowedArea = {0.0, 0.0, 1.0, 1.0};
    std::vector<NormalizedRect> validExclusions;
    for (auto exclusion : excludedAreas) {
        exclusion = clampNormalizedRect(exclusion);
        if (exclusion.isValid()) validExclusions.push_back(exclusion);
    }
    excludedAreas = std::move(validExclusions);
    preferredPosition.x = clamp01(preferredPosition.x);
    preferredPosition.y = clamp01(preferredPosition.y);
    if (fontFamily.empty()) fontFamily = "Segoe UI";
    if (fontFamily.size() > 128) fontFamily.resize(128);
    if (fixedMonitorKey.size() > 512) fixedMonitorKey.resize(512);
}

std::string serializeSettings(const Settings& input) {
    Settings settings = input;
    settings.validateAndNormalize();
    std::ostringstream output;
    output << "# Adaptive OLED Clock settings\n"
           << "version=" << settings.version << '\n'
           << "timeFormat=" << static_cast<int>(settings.timeFormat) << '\n'
           << "showAmPm=" << (settings.showAmPm ? 1 : 0) << '\n'
           << "showSeconds=" << (settings.showSeconds ? 1 : 0) << '\n'
           << "showDate=" << (settings.showDate ? 1 : 0) << '\n'
           << "fontFamily=" << escapeValue(settings.fontFamily) << '\n'
           << "fontWeight=" << static_cast<int>(settings.fontWeight) << '\n'
           << std::setprecision(17)
           << "fontSizeDip=" << settings.fontSizeDip << '\n'
           << "textColor=" << static_cast<int>(settings.textColor.r) << ','
           << static_cast<int>(settings.textColor.g) << ',' << static_cast<int>(settings.textColor.b) << ','
           << static_cast<int>(settings.textColor.a) << '\n'
           << "opacity=" << settings.opacity << '\n'
           << "movementIntervalMinutes=" << settings.movementIntervalMinutes << '\n'
           << "movementMode=" << static_cast<int>(settings.movementMode) << '\n'
           << "microShiftEnabled=" << (settings.microShiftEnabled ? 1 : 0) << '\n'
           << "microShiftRadiusDip=" << settings.microShiftRadiusDip << '\n'
           << "allowedArea=" << rectText(settings.allowedArea) << '\n'
           << "excludedAreas=";
    for (std::size_t index = 0; index < settings.excludedAreas.size(); ++index) {
        if (index != 0) output << ';';
        output << rectText(settings.excludedAreas[index]);
    }
    output << '\n'
           << "edgeMarginDip=" << settings.edgeMarginDip << '\n'
           << "monitorMode=" << static_cast<int>(settings.monitorMode) << '\n'
           << "fixedMonitorKey=" << escapeValue(settings.fixedMonitorKey) << '\n'
           << "preferredPosition=" << settings.preferredPosition.x << ',' << settings.preferredPosition.y << '\n'
           << "preferredPositionEnabled=" << (settings.preferredPositionEnabled ? 1 : 0) << '\n'
           << "hideInFullscreen=" << (settings.hideInFullscreen ? 1 : 0) << '\n'
           << "launchAtStartup=" << (settings.launchAtStartup ? 1 : 0) << '\n'
           << "hotkeyEnabled=" << (settings.hotkeyEnabled ? 1 : 0) << '\n'
           << "clockVisible=" << (settings.clockVisible ? 1 : 0) << '\n'
           << "boostOpacity=" << settings.boostOpacity << '\n'
           << "boostDurationSeconds=" << settings.boostDurationSeconds << '\n';
    return output.str();
}

SettingsLoadResult deserializeSettings(const std::string& text) {
    SettingsLoadResult result;
    result.value = Settings::defaults();
    bool syntaxOk = true;
    const auto properties = parseProperties(text, syntaxOk);
    if (properties.empty()) {
        result.recovered = true;
        result.error = "settings file was empty or had no properties";
        return result;
    }

    int version = 0;
    if (const auto found = properties.find("version"); found != properties.end()) {
        if (!parseInt(found->second, version)) {
            result.recovered = true;
            result.error = "settings version was invalid";
            return result;
        }
    }
    if (version > kCurrentSettingsVersion || version < 0) {
        result.recovered = true;
        result.error = "settings version was unsupported";
        return result;
    }
    result.migrated = version < kCurrentSettingsVersion;
    result.recovered = result.recovered || result.migrated || !syntaxOk;
    bool invalid = !syntaxOk;
    Settings& settings = result.value;
    int integer = 0;

    if (const auto found = properties.find("timeFormat"); found != properties.end()) {
        if (parseInt(found->second, integer)) settings.timeFormat = static_cast<TimeFormat>(integer);
        else invalid = true;
    }
    readBool(properties, "showAmPm", settings.showAmPm, invalid);
    readBool(properties, "showSeconds", settings.showSeconds, invalid);
    readBool(properties, "showDate", settings.showDate, invalid);
    if (const auto found = properties.find("fontFamily"); found != properties.end()) {
        settings.fontFamily = unescapeValue(found->second);
    }
    if (const auto found = properties.find("fontWeight"); found != properties.end()) {
        if (parseInt(found->second, integer)) settings.fontWeight = static_cast<FontWeight>(integer);
        else invalid = true;
    }
    readNumber(properties, "fontSizeDip", settings.fontSizeDip, invalid);
    if (version == 0) readNumber(properties, "fontSize", settings.fontSizeDip, invalid);
    if (const auto found = properties.find("textColor"); found != properties.end()) {
        if (!parseColor(found->second, settings.textColor)) invalid = true;
    }
    readNumber(properties, "opacity", settings.opacity, invalid);
    readNumber(properties, "movementIntervalMinutes", settings.movementIntervalMinutes, invalid);
    if (const auto found = properties.find("movementMode"); found != properties.end()) {
        if (parseInt(found->second, integer)) settings.movementMode = static_cast<MovementMode>(integer);
        else invalid = true;
    }
    readBool(properties, "microShiftEnabled", settings.microShiftEnabled, invalid);
    readNumber(properties, "microShiftRadiusDip", settings.microShiftRadiusDip, invalid);
    if (const auto found = properties.find("allowedArea"); found != properties.end()) {
        if (!parseRect(found->second, settings.allowedArea)) invalid = true;
    }
    if (const auto found = properties.find("excludedAreas"); found != properties.end()) {
        settings.excludedAreas.clear();
        if (!found->second.empty()) {
            std::stringstream exclusions(found->second);
            std::string item;
            while (std::getline(exclusions, item, ';')) {
                NormalizedRect rect;
                if (!parseRect(item, rect)) {
                    invalid = true;
                    break;
                }
                settings.excludedAreas.push_back(rect);
            }
        }
    }
    readNumber(properties, "edgeMarginDip", settings.edgeMarginDip, invalid);
    if (const auto found = properties.find("monitorMode"); found != properties.end()) {
        if (parseInt(found->second, integer)) settings.monitorMode = static_cast<MonitorMode>(integer);
        else invalid = true;
    }
    if (const auto found = properties.find("fixedMonitorKey"); found != properties.end()) {
        settings.fixedMonitorKey = unescapeValue(found->second);
    }
    if (const auto found = properties.find("preferredPosition"); found != properties.end()) {
        if (!parsePoint(found->second, settings.preferredPosition)) invalid = true;
    }
    readBool(properties, "preferredPositionEnabled", settings.preferredPositionEnabled, invalid);
    readBool(properties, "hideInFullscreen", settings.hideInFullscreen, invalid);
    readBool(properties, "launchAtStartup", settings.launchAtStartup, invalid);
    readBool(properties, "hotkeyEnabled", settings.hotkeyEnabled, invalid);
    readBool(properties, "clockVisible", settings.clockVisible, invalid);
    readNumber(properties, "boostOpacity", settings.boostOpacity, invalid);
    readNumber(properties, "boostDurationSeconds", settings.boostDurationSeconds, invalid);

    settings.validateAndNormalize();
    if (invalid) {
        result.recovered = true;
        result.error = "settings contained invalid values; valid values were retained";
    } else if (result.migrated) {
        result.error = "settings migrated to the current schema";
    }
    return result;
}

} // namespace aoc::core

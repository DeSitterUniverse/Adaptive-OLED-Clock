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
                const int decoded = std::stoi(std::string(value.substr(index + 1, 2)), nullptr, 16);
                output.push_back(static_cast<char>(decoded));
                index += 2;
                continue;
            } catch (...) {
                // Keep malformed escapes literal; validation will still protect the settings.
            }
        }
        output.push_back(value[index]);
    }
    return output;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return {};
    }
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

template <typename T>
void parseOptionalNumber(const std::map<std::string, std::string>& properties,
                         const char* key,
                         T& destination,
                         bool& valid) {
    const auto found = properties.find(key);
    if (found == properties.end()) return;
    if constexpr (std::is_same_v<T, double>) {
        double parsed = 0.0;
        valid = valid && parseDouble(found->second, parsed);
        if (valid) destination = parsed;
    } else {
        int parsed = 0;
        valid = valid && parseInt(found->second, parsed);
        if (valid) destination = static_cast<T>(parsed);
    }
}

} // namespace

Settings Settings::defaults() {
    Settings settings;
    settings.version = kCurrentSettingsVersion;
    settings.timeFormat = TimeFormat::Locale;
    settings.showAmPm = true;
    settings.fontSizeDip = 32.0;
    settings.textColor = {176, 176, 176, 255};
    settings.opacity = 0.32;
    settings.movementIntervalMinutes = 5;
    settings.movementMode = MovementMode::WholeScreen;
    settings.allowedArea = {0.0, 0.0, 1.0, 1.0};
    settings.excludedAreas.clear();
    settings.edgeMarginDip = 24.0;
    settings.monitorMode = MonitorMode::FollowPrimary;
    settings.fixedMonitorKey.clear();
    settings.preferredPosition = {0.5, 0.5};
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
    settings.opacity = 0.24;
    settings.boostOpacity = 0.70;
    settings.boostDurationSeconds = 10;
    settings.movementIntervalMinutes = 5;
    settings.edgeMarginDip = 32.0;
    return settings;
}

void Settings::validateAndNormalize() noexcept {
    version = kCurrentSettingsVersion;
    if (static_cast<int>(timeFormat) < static_cast<int>(TimeFormat::Locale) ||
        static_cast<int>(timeFormat) > static_cast<int>(TimeFormat::TwentyFourHour)) {
        timeFormat = TimeFormat::Locale;
    }
    if (static_cast<int>(movementMode) < static_cast<int>(MovementMode::WholeScreen) ||
        static_cast<int>(movementMode) > static_cast<int>(MovementMode::LocalWander)) {
        movementMode = MovementMode::WholeScreen;
    }
    if (static_cast<int>(monitorMode) < static_cast<int>(MonitorMode::FollowPrimary) ||
        static_cast<int>(monitorMode) > static_cast<int>(MonitorMode::Fixed)) {
        monitorMode = MonitorMode::FollowPrimary;
    }
    if (!std::isfinite(fontSizeDip)) fontSizeDip = 32.0;
    if (!std::isfinite(opacity)) opacity = 0.32;
    if (!std::isfinite(boostOpacity)) boostOpacity = 0.85;
    if (!std::isfinite(edgeMarginDip)) edgeMarginDip = 24.0;
    fontSizeDip = std::clamp(fontSizeDip, 8.0, 128.0);
    opacity = std::clamp(opacity, 0.0, 1.0);
    boostOpacity = std::clamp(boostOpacity, 0.0, 1.0);
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
    if (fixedMonitorKey.size() > 512) fixedMonitorKey.resize(512);
}

std::string serializeSettings(const Settings& input) {
    Settings settings = input;
    settings.validateAndNormalize();
    std::ostringstream output;
    output << "# Adaptive OLED Clock C++ settings\n"
           << "version=" << settings.version << '\n'
           << "timeFormat=" << static_cast<int>(settings.timeFormat) << '\n'
           << "showAmPm=" << (settings.showAmPm ? 1 : 0) << '\n'
           << std::setprecision(17)
           << "fontSizeDip=" << settings.fontSizeDip << '\n'
           << "textColor=" << static_cast<int>(settings.textColor.r) << ','
           << static_cast<int>(settings.textColor.g) << ',' << static_cast<int>(settings.textColor.b) << ','
           << static_cast<int>(settings.textColor.a) << '\n'
           << "opacity=" << settings.opacity << '\n'
           << "movementIntervalMinutes=" << settings.movementIntervalMinutes << '\n'
           << "movementMode=" << static_cast<int>(settings.movementMode) << '\n'
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
    const auto versionFound = properties.find("version");
    if (versionFound != properties.end() && !parseInt(versionFound->second, version)) {
        result.recovered = true;
        result.error = "settings version was invalid";
        return result;
    }
    if (version > kCurrentSettingsVersion) {
        result.recovered = true;
        result.error = "settings version was newer than this build";
        return result;
    }
    if (version < 0) {
        result.recovered = true;
        result.error = "settings version was negative";
        return result;
    }
    if (version == 0) result.recovered = true;
    if (!syntaxOk) {
        result.recovered = true;
        result.error = "settings contained malformed lines";
        return result;
    }

    Settings& settings = result.value;
    bool valid = true;
    int integer = 0;
    if (const auto found = properties.find("timeFormat"); found != properties.end()) {
        valid = parseInt(found->second, integer);
        if (valid) settings.timeFormat = static_cast<TimeFormat>(integer);
    }
    if (const auto found = properties.find("showAmPm"); found != properties.end()) valid = parseBool(found->second, settings.showAmPm) && valid;
    parseOptionalNumber(properties, "fontSizeDip", settings.fontSizeDip, valid);
    if (version == 0) parseOptionalNumber(properties, "fontSize", settings.fontSizeDip, valid);
    if (const auto found = properties.find("textColor"); found != properties.end()) valid = parseColor(found->second, settings.textColor) && valid;
    parseOptionalNumber(properties, "opacity", settings.opacity, valid);
    parseOptionalNumber(properties, "movementIntervalMinutes", settings.movementIntervalMinutes, valid);
    if (const auto found = properties.find("movementMode"); found != properties.end()) {
        valid = parseInt(found->second, integer) && valid;
        if (valid) settings.movementMode = static_cast<MovementMode>(integer);
    }
    if (const auto found = properties.find("allowedArea"); found != properties.end()) valid = parseRect(found->second, settings.allowedArea) && valid;
    if (const auto found = properties.find("excludedAreas"); found != properties.end() && !found->second.empty()) {
        settings.excludedAreas.clear();
        std::stringstream exclusions(found->second);
        std::string item;
        while (std::getline(exclusions, item, ';')) {
            NormalizedRect rect;
            if (!parseRect(item, rect)) {
                valid = false;
                break;
            }
            settings.excludedAreas.push_back(rect);
        }
    }
    parseOptionalNumber(properties, "edgeMarginDip", settings.edgeMarginDip, valid);
    if (const auto found = properties.find("monitorMode"); found != properties.end()) {
        valid = parseInt(found->second, integer) && valid;
        if (valid) settings.monitorMode = static_cast<MonitorMode>(integer);
    }
    if (const auto found = properties.find("fixedMonitorKey"); found != properties.end()) settings.fixedMonitorKey = unescapeValue(found->second);
    if (const auto found = properties.find("preferredPosition"); found != properties.end()) {
        std::stringstream stream(found->second);
        char comma = 0;
        valid = static_cast<bool>(stream >> settings.preferredPosition.x >> comma >> settings.preferredPosition.y) && comma == ',' && valid;
    }
    if (const auto found = properties.find("hideInFullscreen"); found != properties.end()) valid = parseBool(found->second, settings.hideInFullscreen) && valid;
    if (const auto found = properties.find("launchAtStartup"); found != properties.end()) valid = parseBool(found->second, settings.launchAtStartup) && valid;
    if (const auto found = properties.find("hotkeyEnabled"); found != properties.end()) valid = parseBool(found->second, settings.hotkeyEnabled) && valid;
    if (const auto found = properties.find("clockVisible"); found != properties.end()) valid = parseBool(found->second, settings.clockVisible) && valid;
    parseOptionalNumber(properties, "boostOpacity", settings.boostOpacity, valid);
    parseOptionalNumber(properties, "boostDurationSeconds", settings.boostDurationSeconds, valid);

    if (!valid) {
        result.value = Settings::defaults();
        result.recovered = true;
        result.error = "settings contained an invalid value";
        return result;
    }
    settings.validateAndNormalize();
    return result;
}

} // namespace aoc::core

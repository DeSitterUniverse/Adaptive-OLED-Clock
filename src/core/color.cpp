#include "aoc/core/color.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace aoc::core {
namespace {

int hexDigit(char value) noexcept {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

} // namespace

HsvColor rgbToHsv(Color color) noexcept {
    const double red = static_cast<double>(color.r) / 255.0;
    const double green = static_cast<double>(color.g) / 255.0;
    const double blue = static_cast<double>(color.b) / 255.0;
    const double maximum = std::max({red, green, blue});
    const double minimum = std::min({red, green, blue});
    const double delta = maximum - minimum;

    double hue = 0.0;
    if (delta > 0.0) {
        if (maximum == red) hue = 60.0 * std::fmod((green - blue) / delta, 6.0);
        else if (maximum == green) hue = 60.0 * ((blue - red) / delta + 2.0);
        else hue = 60.0 * ((red - green) / delta + 4.0);
    }
    if (hue < 0.0) hue += 360.0;
    return {hue, maximum <= 0.0 ? 0.0 : delta / maximum, maximum};
}

Color hsvToRgb(HsvColor color, std::uint8_t alpha) noexcept {
    double hue = std::fmod(color.hue, 360.0);
    if (hue < 0.0) hue += 360.0;
    const double saturation = std::clamp(color.saturation, 0.0, 1.0);
    const double value = std::clamp(color.value, 0.0, 1.0);
    const double chroma = value * saturation;
    const double second = chroma * (1.0 - std::abs(std::fmod(hue / 60.0, 2.0) - 1.0));
    const double match = value - chroma;

    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    if (hue < 60.0) { red = chroma; green = second; }
    else if (hue < 120.0) { red = second; green = chroma; }
    else if (hue < 180.0) { green = chroma; blue = second; }
    else if (hue < 240.0) { green = second; blue = chroma; }
    else if (hue < 300.0) { red = second; blue = chroma; }
    else { red = chroma; blue = second; }

    const auto channel = [match](double component) {
        return static_cast<std::uint8_t>(std::lround(std::clamp(component + match, 0.0, 1.0) * 255.0));
    };
    return {channel(red), channel(green), channel(blue), alpha};
}

std::string colorToHex(Color color) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result(7, '#');
    result[1] = digits[color.r >> 4U];
    result[2] = digits[color.r & 0x0FU];
    result[3] = digits[color.g >> 4U];
    result[4] = digits[color.g & 0x0FU];
    result[5] = digits[color.b >> 4U];
    result[6] = digits[color.b & 0x0FU];
    return result;
}

std::optional<Color> parseHexColor(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
    if (!text.empty() && text.front() == '#') text.remove_prefix(1);
    if (text.size() != 3 && text.size() != 6) return std::nullopt;

    std::array<std::uint8_t, 3> channels{};
    for (std::size_t index = 0; index < channels.size(); ++index) {
        if (text.size() == 3) {
            const int digit = hexDigit(text[index]);
            if (digit < 0) return std::nullopt;
            channels[index] = static_cast<std::uint8_t>(digit * 17);
        } else {
            const int high = hexDigit(text[index * 2]);
            const int low = hexDigit(text[index * 2 + 1]);
            if (high < 0 || low < 0) return std::nullopt;
            channels[index] = static_cast<std::uint8_t>((high << 4) | low);
        }
    }
    return Color{channels[0], channels[1], channels[2], 255};
}

} // namespace aoc::core

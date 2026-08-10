#pragma once

#include "aoc/core/types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace aoc::core {

struct HsvColor {
    double hue{0.0};
    double saturation{0.0};
    double value{0.0};
};

[[nodiscard]] HsvColor rgbToHsv(Color color) noexcept;
[[nodiscard]] Color hsvToRgb(HsvColor color, std::uint8_t alpha = 255) noexcept;
[[nodiscard]] std::string colorToHex(Color color);
[[nodiscard]] std::optional<Color> parseHexColor(std::string_view text) noexcept;

} // namespace aoc::core

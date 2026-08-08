#pragma once

#include "aoc/core/types.h"

#include <cstdint>

namespace aoc::core {

[[nodiscard]] double clamp01(double value) noexcept;
[[nodiscard]] NormalizedRect normalizeRect(NormalizedRect rect) noexcept;
[[nodiscard]] NormalizedRect clampNormalizedRect(NormalizedRect rect) noexcept;
[[nodiscard]] RectD normalizedToPhysical(const NormalizedRect& normalized, const RectI& monitorBounds) noexcept;
[[nodiscard]] RectI normalizedToPhysicalPixels(const NormalizedRect& normalized, const RectI& monitorBounds) noexcept;
[[nodiscard]] NormalizedRect physicalToNormalized(const RectI& physical, const RectI& monitorBounds) noexcept;
[[nodiscard]] PointD normalizedPointToPhysical(NormalizedPoint point, const RectI& monitorBounds) noexcept;
[[nodiscard]] NormalizedPoint physicalPointToNormalized(PointD point, const RectI& monitorBounds) noexcept;
[[nodiscard]] double pixelsToDip(double pixels, std::uint32_t dpi) noexcept;
[[nodiscard]] double dipToPixels(double dips, std::uint32_t dpi) noexcept;
[[nodiscard]] RectD pixelsToDip(const RectI& pixels, std::uint32_t dpi) noexcept;
[[nodiscard]] RectI dipToPixels(const RectD& dips, std::uint32_t dpi) noexcept;
[[nodiscard]] RectI clampRectTo(const RectI& rect, const RectI& bounds) noexcept;

} // namespace aoc::core

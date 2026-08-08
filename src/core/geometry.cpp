#include "aoc/core/geometry.h"

#include <algorithm>
#include <cmath>

namespace aoc::core {

double clamp01(double value) noexcept {
    return std::clamp(value, 0.0, 1.0);
}

NormalizedRect normalizeRect(NormalizedRect rect) noexcept {
    if (rect.left > rect.right) {
        std::swap(rect.left, rect.right);
    }
    if (rect.top > rect.bottom) {
        std::swap(rect.top, rect.bottom);
    }
    return rect;
}

NormalizedRect clampNormalizedRect(NormalizedRect rect) noexcept {
    rect = normalizeRect(rect);
    rect.left = clamp01(rect.left);
    rect.top = clamp01(rect.top);
    rect.right = clamp01(rect.right);
    rect.bottom = clamp01(rect.bottom);
    return rect;
}

RectD normalizedToPhysical(const NormalizedRect& normalized, const RectI& monitorBounds) noexcept {
    return {static_cast<double>(monitorBounds.left) + normalized.left * monitorBounds.width(),
            static_cast<double>(monitorBounds.top) + normalized.top * monitorBounds.height(),
            static_cast<double>(monitorBounds.left) + normalized.right * monitorBounds.width(),
            static_cast<double>(monitorBounds.top) + normalized.bottom * monitorBounds.height()};
}

RectI normalizedToPhysicalPixels(const NormalizedRect& normalized, const RectI& monitorBounds) noexcept {
    const RectD result = normalizedToPhysical(normalized, monitorBounds);
    return {static_cast<int>(std::floor(result.left)), static_cast<int>(std::floor(result.top)),
            static_cast<int>(std::ceil(result.right)), static_cast<int>(std::ceil(result.bottom))};
}

NormalizedRect physicalToNormalized(const RectI& physical, const RectI& monitorBounds) noexcept {
    if (!monitorBounds.isValid()) {
        return {};
    }
    return clampNormalizedRect({
        static_cast<double>(physical.left - monitorBounds.left) / monitorBounds.width(),
        static_cast<double>(physical.top - monitorBounds.top) / monitorBounds.height(),
        static_cast<double>(physical.right - monitorBounds.left) / monitorBounds.width(),
        static_cast<double>(physical.bottom - monitorBounds.top) / monitorBounds.height(),
    });
}

PointD normalizedPointToPhysical(NormalizedPoint point, const RectI& monitorBounds) noexcept {
    point.x = clamp01(point.x);
    point.y = clamp01(point.y);
    return {static_cast<double>(monitorBounds.left) + point.x * monitorBounds.width(),
            static_cast<double>(monitorBounds.top) + point.y * monitorBounds.height()};
}

NormalizedPoint physicalPointToNormalized(PointD point, const RectI& monitorBounds) noexcept {
    if (!monitorBounds.isValid()) {
        return {};
    }
    return {clamp01((point.x - monitorBounds.left) / monitorBounds.width()),
            clamp01((point.y - monitorBounds.top) / monitorBounds.height())};
}

double pixelsToDip(double pixels, std::uint32_t dpi) noexcept {
    return pixels * 96.0 / static_cast<double>(dpi == 0 ? 96 : dpi);
}

double dipToPixels(double dips, std::uint32_t dpi) noexcept {
    return dips * static_cast<double>(dpi == 0 ? 96 : dpi) / 96.0;
}

RectD pixelsToDip(const RectI& pixels, std::uint32_t dpi) noexcept {
    return {pixelsToDip(pixels.left, dpi), pixelsToDip(pixels.top, dpi),
            pixelsToDip(pixels.right, dpi), pixelsToDip(pixels.bottom, dpi)};
}

RectI dipToPixels(const RectD& dips, std::uint32_t dpi) noexcept {
    return {static_cast<int>(std::lround(dipToPixels(dips.left, dpi))),
            static_cast<int>(std::lround(dipToPixels(dips.top, dpi))),
            static_cast<int>(std::lround(dipToPixels(dips.right, dpi))),
            static_cast<int>(std::lround(dipToPixels(dips.bottom, dpi)))};
}

RectI clampRectTo(const RectI& rect, const RectI& bounds) noexcept {
    if (!rect.isValid() || !bounds.isValid()) {
        return {};
    }
    RectI result = rect;
    const int width = rect.width();
    const int height = rect.height();
    if (result.left < bounds.left) {
        result.left = bounds.left;
        result.right = result.left + width;
    }
    if (result.top < bounds.top) {
        result.top = bounds.top;
        result.bottom = result.top + height;
    }
    if (result.right > bounds.right) {
        result.right = bounds.right;
        result.left = result.right - width;
    }
    if (result.bottom > bounds.bottom) {
        result.bottom = bounds.bottom;
        result.top = result.bottom - height;
    }
    return bounds.contains(result) ? result : RectI{};
}

} // namespace aoc::core

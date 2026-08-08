#include "aoc/core/types.h"

#include <algorithm>

namespace aoc::core {

double RectD::area() const noexcept {
    return isValid() ? width() * height() : 0.0;
}

bool RectD::isValid() const noexcept {
    return right > left && bottom > top;
}

bool RectD::contains(const RectD& other) const noexcept {
    return isValid() && other.isValid() && other.left >= left && other.top >= top &&
           other.right <= right && other.bottom <= bottom;
}

bool RectD::contains(PointD point) const noexcept {
    return isValid() && point.x >= left && point.x <= right && point.y >= top && point.y <= bottom;
}

bool RectD::intersects(const RectD& other) const noexcept {
    return std::max(left, other.left) < std::min(right, other.right) &&
           std::max(top, other.top) < std::min(bottom, other.bottom);
}

RectD RectD::intersection(const RectD& other) const noexcept {
    return {std::max(left, other.left), std::max(top, other.top),
            std::min(right, other.right), std::min(bottom, other.bottom)};
}

bool RectI::contains(const RectI& other) const noexcept {
    return isValid() && other.isValid() && other.left >= left && other.top >= top &&
           other.right <= right && other.bottom <= bottom;
}

bool RectI::intersects(const RectI& other) const noexcept {
    return std::max(left, other.left) < std::min(right, other.right) &&
           std::max(top, other.top) < std::min(bottom, other.bottom);
}

RectI RectI::intersection(const RectI& other) const noexcept {
    return {std::max(left, other.left), std::max(top, other.top),
            std::min(right, other.right), std::min(bottom, other.bottom)};
}

double NormalizedRect::area() const noexcept {
    return isValid() ? width() * height() : 0.0;
}

bool NormalizedRect::isValid() const noexcept {
    return right > left && bottom > top;
}

NormalizedRect NormalizedRect::intersection(const NormalizedRect& other) const noexcept {
    return {std::max(left, other.left), std::max(top, other.top),
            std::min(right, other.right), std::min(bottom, other.bottom)};
}

} // namespace aoc::core

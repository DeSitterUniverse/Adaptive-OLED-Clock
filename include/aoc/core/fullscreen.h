#pragma once

#include "aoc/core/types.h"

namespace aoc::core {

struct ForegroundWindowSnapshot {
    bool visible{false};
    bool shellWindow{false};
    bool toolWindow{false};
    RectI windowRectPx;
    RectI extendedFrameRectPx;
    RectI monitorBoundsPx;
    bool minimized{false};
};

[[nodiscard]] bool isFullscreenLike(const ForegroundWindowSnapshot& snapshot,
                                    int tolerancePixels = 3) noexcept;

} // namespace aoc::core

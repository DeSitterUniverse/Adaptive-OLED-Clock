#include "aoc/core/fullscreen.h"

#include <algorithm>
#include <cstdlib>

namespace aoc::core {

bool isFullscreenLike(const ForegroundWindowSnapshot& snapshot, int tolerancePixels) noexcept {
    if (!snapshot.visible || snapshot.minimized || snapshot.shellWindow || snapshot.toolWindow ||
        !snapshot.monitorBoundsPx.isValid()) {
        return false;
    }
    const RectI frame = snapshot.extendedFrameRectPx.isValid() ? snapshot.extendedFrameRectPx : snapshot.windowRectPx;
    if (!frame.isValid()) return false;
    const int tolerance = std::max(0, tolerancePixels);
    return frame.left <= snapshot.monitorBoundsPx.left + tolerance &&
           frame.top <= snapshot.monitorBoundsPx.top + tolerance &&
           frame.right >= snapshot.monitorBoundsPx.right - tolerance &&
           frame.bottom >= snapshot.monitorBoundsPx.bottom - tolerance;
}

} // namespace aoc::core

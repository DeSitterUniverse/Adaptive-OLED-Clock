#include "aoc/core/placement.h"

#include "aoc/core/geometry.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace aoc::core {
namespace {

RectI movementBounds(const PlacementContext& context) {
    if (!context.monitorBoundsPx.isValid() || !context.policyBoundsPx.isValid()) return {};
    const int margin = static_cast<int>(std::lround(dipToPixels(context.edgeMarginDip, context.dpi)));
    RectI result{context.policyBoundsPx.left + margin, context.policyBoundsPx.top + margin,
                 context.policyBoundsPx.right - margin, context.policyBoundsPx.bottom - margin};
    result = result.intersection(normalizedToPhysicalPixels(context.allowedArea, context.monitorBoundsPx));
    return result;
}

SizeD clockSizePx(const PlacementContext& context) {
    return {std::max(1.0, static_cast<double>(std::lround(dipToPixels(context.clockSizeDip.width, context.dpi)))),
            std::max(1.0, static_cast<double>(std::lround(dipToPixels(context.clockSizeDip.height, context.dpi))))};
}

RectI centeredAt(PointD center, SizeD size) {
    const int width = static_cast<int>(std::lround(size.width));
    const int height = static_cast<int>(std::lround(size.height));
    const int left = static_cast<int>(std::lround(center.x - width / 2.0));
    const int top = static_cast<int>(std::lround(center.y - height / 2.0));
    return {left, top, left + width, top + height};
}

std::vector<RectI> deduplicate(std::vector<RectI> candidates) {
    std::vector<RectI> result;
    for (const RectI& candidate : candidates) {
        if (!candidate.isValid()) continue;
        if (std::find(result.begin(), result.end(), candidate) == result.end()) result.push_back(candidate);
    }
    return result;
}

double iou(const RectI& first, const RectI& second) {
    const RectI overlap = first.intersection(second);
    const double overlapArea = static_cast<double>(std::max(0, overlap.width())) * std::max(0, overlap.height());
    const double firstArea = static_cast<double>(first.width()) * first.height();
    const double secondArea = static_cast<double>(second.width()) * second.height();
    const double unionArea = firstArea + secondArea - overlapArea;
    return unionArea <= 0.0 ? 0.0 : overlapArea / unionArea;
}

} // namespace

RectI paddedPlacementRect(const RectI& candidate, double paddingDip, std::uint32_t dpi) noexcept {
    if (!candidate.isValid()) return {};
    const int padding = std::max(0, static_cast<int>(std::lround(dipToPixels(paddingDip, dpi))));
    return {candidate.left - padding, candidate.top - padding,
            candidate.right + padding, candidate.bottom + padding};
}

bool isValidPlacement(const RectI& candidate, const PlacementContext& context) {
    const RectI bounds = movementBounds(context);
    const RectI surface = paddedPlacementRect(candidate, context.surfacePaddingDip, context.dpi);
    if (!candidate.isValid() || !surface.isValid() || !bounds.contains(surface)) return false;
    return true;
}

RectI fitPlacementNear(const RectI& desired, const PlacementContext& context) noexcept {
    if (!desired.isValid()) return {};
    const RectI bounds = movementBounds(context);
    const int padding = std::max(0, static_cast<int>(std::lround(dipToPixels(context.surfacePaddingDip,
                                                                             context.dpi))));
    const RectI surface = paddedPlacementRect(desired, context.surfacePaddingDip, context.dpi);
    const RectI fittedSurface = clampRectTo(surface, bounds);
    if (!fittedSurface.isValid()) return {};
    const RectI fitted{fittedSurface.left + padding, fittedSurface.top + padding,
                       fittedSurface.right - padding, fittedSurface.bottom - padding};
    return isValidPlacement(fitted, context) ? fitted : RectI{};
}

std::vector<RectI> generateCandidates(const PlacementContext& context) {
    const RectI bounds = movementBounds(context);
    const SizeD size = clockSizePx(context);
    if (!bounds.isValid() || size.width > bounds.width() || size.height > bounds.height()) return {};
    const double minCenterX = bounds.left + size.width / 2.0;
    const double maxCenterX = bounds.right - size.width / 2.0;
    const double minCenterY = bounds.top + size.height / 2.0;
    const double maxCenterY = bounds.bottom - size.height / 2.0;
    std::vector<RectI> result;
    auto addCenter = [&](PointD center) {
        const RectI candidate = centeredAt(center, size);
        if (isValidPlacement(candidate, context)) result.push_back(candidate);
    };

    if (context.mode == MovementMode::FourCorners) {
        addCenter({minCenterX, minCenterY});
        addCenter({maxCenterX, minCenterY});
        addCenter({minCenterX, maxCenterY});
        addCenter({maxCenterX, maxCenterY});
    } else if (context.mode == MovementMode::EdgeOnly) {
        // Edge-only deliberately samples the perimeter after margins and padding; there are no interior anchors.
        constexpr int samples = 17;
        for (int index = 0; index < samples; ++index) {
            const double fraction = samples == 1 ? 0.0 : static_cast<double>(index) / (samples - 1);
            const double x = minCenterX + (maxCenterX - minCenterX) * fraction;
            const double y = minCenterY + (maxCenterY - minCenterY) * fraction;
            addCenter({x, minCenterY});
            addCenter({x, maxCenterY});
            addCenter({minCenterX, y});
            addCenter({maxCenterX, y});
        }
    } else if (context.mode == MovementMode::WholeScreen) {
        constexpr int columns = 13;
        constexpr int rows = 9;
        for (int row = 0; row < rows; ++row) {
            const double y = rows == 1 ? minCenterY : minCenterY + (maxCenterY - minCenterY) * row / (rows - 1);
            for (int column = 0; column < columns; ++column) {
                const double x = columns == 1 ? minCenterX : minCenterX + (maxCenterX - minCenterX) * column / (columns - 1);
                addCenter({x, y});
            }
        }
    } else {
        // Local mode is a fixed pixel region around the position selected with
        // Position clock. Without an explicit anchor it starts at the middle of
        // the allowed area. Candidate centres are clipped at the usable border.
        const PointD requested = context.localAnchorPx.value_or(bounds.center());
        const PointD preferred{std::clamp(requested.x, minCenterX, maxCenterX),
                               std::clamp(requested.y, minCenterY, maxCenterY)};
        const double radius = static_cast<double>(std::max(1, context.localRadiusPx));
        const double minLocalX = std::max(minCenterX, preferred.x - radius);
        const double maxLocalX = std::min(maxCenterX, preferred.x + radius);
        const double minLocalY = std::max(minCenterY, preferred.y - radius);
        const double maxLocalY = std::min(maxCenterY, preferred.y + radius);
        constexpr int columns = 7;
        constexpr int rows = 7;
        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                const double x = minLocalX + (maxLocalX - minLocalX) * column / (columns - 1);
                const double y = minLocalY + (maxLocalY - minLocalY) * row / (rows - 1);
                addCenter({x, y});
            }
        }
        addCenter(preferred);
    }
    return deduplicate(std::move(result));
}

std::optional<PlacementCandidate> choosePlacement(const PlacementContext& context,
                                                  const ExposureMap& exposure) {
    const auto candidates = generateCandidates(context);
    if (candidates.empty()) return std::nullopt;
    std::mt19937_64 generator(context.randomSeed);
    std::uniform_real_distribution<double> random(0.0, 1.0);
    std::optional<PlacementCandidate> best;
    for (const RectI& candidate : candidates) {
        if (context.previousRectPx.has_value() && candidate == *context.previousRectPx && candidates.size() > 1) continue;
        const NormalizedRect normalized = physicalToNormalized(candidate, context.monitorBoundsPx);
        const double exposureScore = exposure.weightedExposure(normalized);
        double recentPenalty = 0.0;
        const auto addHistoryPenalty = [&](const RectI& previous, double weight) {
            const double overlap = iou(candidate, previous);
            // Avoid repeats and overlap without rewarding the farthest point.
            // A distance reward caused Whole screen to bounce among edges and
            // corners even though its candidate grid contained the interior.
            recentPenalty += weight * overlap * 520.0;
            if (candidate == previous) recentPenalty += weight * 5000.0;
        };
        if (context.previousRectPx.has_value()) addHistoryPenalty(*context.previousRectPx, 1.0);
        for (std::size_t index = 0; index < context.recentMacroRects.size(); ++index) {
            const double weight = 1.0 / static_cast<double>(index + 1);
            addHistoryPenalty(context.recentMacroRects[index], weight);
        }
        // Exposure is a gentle tie-breaker, not an ever-growing force that can
        // override movement cadence and recent-position avoidance after long use.
        const double exposurePenalty = std::log1p(std::max(0.0, exposureScore)) * 0.35;
        const double score = exposurePenalty + recentPenalty + random(generator);
        if (!best.has_value() || score < best->score) {
            best = PlacementCandidate{candidate, exposureScore, score};
        }
    }
    if (!best.has_value()) {
        const RectI candidate = candidates.front();
        best = PlacementCandidate{candidate, exposure.weightedExposure(physicalToNormalized(candidate, context.monitorBoundsPx)), 0.0};
    }
    return best;
}

} // namespace aoc::core

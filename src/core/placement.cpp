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

bool isValidPlacement(const RectI& candidate, const PlacementContext& context) {
    const RectI bounds = movementBounds(context);
    if (!candidate.isValid() || !bounds.contains(candidate)) return false;
    for (const NormalizedRect& exclusion : context.excludedAreas) {
        if (candidate.intersects(normalizedToPhysicalPixels(exclusion, context.monitorBoundsPx))) return false;
    }
    return true;
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

    if (context.mode == MovementMode::WholeScreen) {
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
        const PointD preferred = context.preferredCenter.has_value()
                                     ? normalizedPointToPhysical(*context.preferredCenter, context.monitorBoundsPx)
                                     : bounds.center();
        const double stepX = std::max(size.width * 1.35, 18.0);
        const double stepY = std::max(size.height * 1.35, 18.0);
        for (int row = -4; row <= 4; ++row) {
            for (int column = -5; column <= 5; ++column) {
                const double x = preferred.x + column * stepX;
                const double y = preferred.y + row * stepY;
                if (x >= minCenterX - size.width && x <= maxCenterX + size.width &&
                    y >= minCenterY - size.height && y <= maxCenterY + size.height) {
                    addCenter({x, y});
                }
            }
        }
        addCenter(preferred);
    }
    return deduplicate(std::move(result));
}

std::optional<RectI> preferredPlacement(const PlacementContext& context) {
    if (!context.preferredCenter.has_value()) return std::nullopt;
    const SizeD size = clockSizePx(context);
    const RectI candidate = centeredAt(normalizedPointToPhysical(*context.preferredCenter, context.monitorBoundsPx), size);
    return isValidPlacement(candidate, context) ? std::optional<RectI>(candidate) : std::nullopt;
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
        if (context.previousRectPx.has_value()) {
            const double overlap = iou(candidate, *context.previousRectPx);
            const PointD currentCenter = candidate.center();
            const PointD previousCenter = context.previousRectPx->center();
            const double distance = std::hypot(currentCenter.x - previousCenter.x, currentCenter.y - previousCenter.y);
            const double scale = std::max(1.0, std::hypot(candidate.width(), candidate.height()));
            recentPenalty += overlap * 300.0;
            recentPenalty += std::exp(-distance / (scale * 2.5)) * 55.0;
            if (overlap > 0.70) recentPenalty += 250.0;
        }
        const double score = exposureScore + recentPenalty + random(generator) * 0.03;
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

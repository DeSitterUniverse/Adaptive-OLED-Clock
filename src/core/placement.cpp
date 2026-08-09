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

double proximityPenalty(const RectI& candidate, const RectI& previous) {
    const PointD currentCenter = candidate.center();
    const PointD previousCenter = previous.center();
    const double distance = std::hypot(currentCenter.x - previousCenter.x, currentCenter.y - previousCenter.y);
    const double scale = std::max(1.0, std::hypot(candidate.width(), candidate.height()));
    return std::exp(-distance / (scale * 2.5)) * 70.0;
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
    for (const NormalizedRect& exclusion : context.excludedAreas) {
        if (surface.intersects(normalizedToPhysicalPixels(exclusion, context.monitorBoundsPx))) return false;
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

    if (context.mode == MovementMode::EdgeOnly) {
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
        const auto addHistoryPenalty = [&](const RectI& previous, double weight) {
            const double overlap = iou(candidate, previous);
            recentPenalty += weight * (overlap * 520.0 + proximityPenalty(candidate, previous));
            if (candidate == previous) recentPenalty += weight * 5000.0;
        };
        if (context.previousRectPx.has_value()) addHistoryPenalty(*context.previousRectPx, 1.0);
        for (std::size_t index = 0; index < context.recentMacroRects.size(); ++index) {
            const double weight = 1.0 / static_cast<double>(index + 1);
            addHistoryPenalty(context.recentMacroRects[index], weight);
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

RectI applyBoundedMicroShift(const RectI& currentRect,
                             const RectI& macroAnchorRect,
                             double radiusDip,
                             std::uint32_t dpi,
                             std::uint64_t randomSeed) noexcept {
    if (!currentRect.isValid()) return {};
    const RectI origin = macroAnchorRect.isValid() ? macroAnchorRect : currentRect;
    const int radius = std::max(0, static_cast<int>(std::lround(dipToPixels(std::max(0.0, radiusDip), dpi))));
    if (radius == 0) return origin;
    std::mt19937_64 generator(randomSeed);
    std::uniform_int_distribution<int> offset(-radius, radius);
    const int dx = offset(generator);
    const int dy = offset(generator);
    return {origin.left + dx, origin.top + dy,
            origin.right + dx, origin.bottom + dy};
}

} // namespace aoc::core

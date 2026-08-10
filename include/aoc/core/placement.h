#pragma once

#include "aoc/core/exposure.h"
#include "aoc/core/types.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace aoc::core {

struct PlacementContext {
    RectI monitorBoundsPx;
    RectI policyBoundsPx;
    std::uint32_t dpi{96};
    SizeD clockSizeDip{80.0, 32.0};
    double edgeMarginDip{0.0};
    NormalizedRect allowedArea{};
    MovementMode mode{MovementMode::EdgeOnly};
    std::optional<PointD> localAnchorPx;
    int localRadiusPx{100};
    std::optional<RectI> previousRectPx;
    std::vector<RectI> recentMacroRects;
    double surfacePaddingDip{0.0};
    std::uint64_t randomSeed{0xA0C0C0DEULL};
};

struct PlacementCandidate {
    RectI boundsPx;
    double exposureScore{0.0};
    double score{0.0};
};

[[nodiscard]] std::vector<RectI> generateCandidates(const PlacementContext& context);
[[nodiscard]] bool isValidPlacement(const RectI& candidate, const PlacementContext& context);
[[nodiscard]] RectI fitPlacementNear(const RectI& desired, const PlacementContext& context) noexcept;
[[nodiscard]] RectI paddedPlacementRect(const RectI& candidate,
                                        double paddingDip,
                                        std::uint32_t dpi) noexcept;
[[nodiscard]] std::optional<PlacementCandidate> choosePlacement(const PlacementContext& context,
                                                                  const ExposureMap& exposure);

} // namespace aoc::core

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
    double edgeMarginDip{24.0};
    NormalizedRect allowedArea{};
    std::vector<NormalizedRect> excludedAreas;
    MovementMode mode{MovementMode::WholeScreen};
    std::optional<NormalizedPoint> preferredCenter;
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
[[nodiscard]] RectI paddedPlacementRect(const RectI& candidate,
                                        double paddingDip,
                                        std::uint32_t dpi) noexcept;
[[nodiscard]] std::optional<RectI> preferredPlacement(const PlacementContext& context);
[[nodiscard]] std::optional<PlacementCandidate> choosePlacement(const PlacementContext& context,
                                                                  const ExposureMap& exposure);
[[nodiscard]] RectI applyBoundedMicroShift(const RectI& currentRect,
                                            const RectI& macroAnchorRect,
                                            double radiusDip,
                                            std::uint32_t dpi,
                                            std::uint64_t randomSeed) noexcept;

} // namespace aoc::core

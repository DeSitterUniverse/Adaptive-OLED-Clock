#pragma once

#include "aoc/core/types.h"

#include <array>
#include <chrono>
#include <map>
#include <optional>
#include <string>

namespace aoc::core {

struct ExposureMap {
    std::array<double, kExposureCellCount> seconds{};

    void charge(const NormalizedRect& renderedBounds, double elapsedSeconds) noexcept;
    [[nodiscard]] double weightedExposure(const NormalizedRect& renderedBounds) const noexcept;
    [[nodiscard]] double totalSeconds() const noexcept;
    [[nodiscard]] double cell(std::size_t column, std::size_t row) const noexcept;
    [[nodiscard]] std::pair<std::size_t, std::size_t> leastExposedCell() const noexcept;
    [[nodiscard]] std::pair<std::size_t, std::size_t> mostExposedCell() const noexcept;
    [[nodiscard]] double imbalance() const noexcept;
    void reset() noexcept { seconds.fill(0.0); }
};

class ExposureStore {
public:
    [[nodiscard]] ExposureMap& forMonitor(const std::string& stableKey);
    [[nodiscard]] const ExposureMap* find(const std::string& stableKey) const noexcept;
    [[nodiscard]] const std::map<std::string, ExposureMap>& maps() const noexcept { return maps_; }
    void replace(std::map<std::string, ExposureMap> maps) { maps_ = std::move(maps); }
    void clear() noexcept { maps_.clear(); }

private:
    std::map<std::string, ExposureMap> maps_;
};

class ExposureTracker {
public:
    explicit ExposureTracker(ExposureStore& store) : store_(store) {}

    void setState(const std::string& monitorKey,
                  NormalizedRect renderedBounds,
                  bool effectivelyVisible,
                  std::chrono::steady_clock::time_point now);
    void checkpoint(std::chrono::steady_clock::time_point now);
    void settle(std::chrono::steady_clock::time_point now);
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] const std::string& monitorKey() const noexcept { return monitorKey_; }

private:
    void settleInternal(std::chrono::steady_clock::time_point now);

    ExposureStore& store_;
    std::string monitorKey_;
    NormalizedRect renderedBounds_{};
    bool active_{false};
    bool initialized_{false};
    std::chrono::steady_clock::time_point lastSettlement_{};
};

struct ExposureLoadResult {
    ExposureStore value;
    bool recovered{false};
    std::string error;
};

[[nodiscard]] std::string serializeExposure(const ExposureStore& store);
[[nodiscard]] ExposureLoadResult deserializeExposure(const std::string& text);

} // namespace aoc::core

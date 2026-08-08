#pragma once

#include "aoc/core/settings.h"
#include "aoc/core/types.h"

#include <optional>
#include <vector>

namespace aoc::core {

struct MonitorSelection {
    std::optional<MonitorInfo> selected;
    bool usedPrimaryFallback{false};
};

[[nodiscard]] MonitorSelection selectMonitor(const std::vector<MonitorInfo>& monitors,
                                             const Settings& settings);

} // namespace aoc::core

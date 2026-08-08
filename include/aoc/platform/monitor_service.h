#pragma once

#include "aoc/core/types.h"

#include <vector>

namespace aoc::platform {

class MonitorService {
public:
    [[nodiscard]] std::vector<core::MonitorInfo> enumerate() const;
};

} // namespace aoc::platform

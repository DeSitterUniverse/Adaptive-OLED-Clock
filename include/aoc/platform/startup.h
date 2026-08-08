#pragma once

#include <string>

namespace aoc::platform {

[[nodiscard]] bool setLaunchAtStartup(bool enabled, const std::wstring& executablePath);
[[nodiscard]] bool launchAtStartupEnabled(const std::wstring& executablePath);

} // namespace aoc::platform

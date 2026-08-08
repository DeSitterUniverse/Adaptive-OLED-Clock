#pragma once

#include "aoc/core/exposure.h"
#include "aoc/core/settings.h"

#include <filesystem>
#include <string>

namespace aoc::platform {

struct DataPaths {
    std::filesystem::path root;
    std::filesystem::path settingsFile;
    std::filesystem::path exposureFile;
    std::filesystem::path logFile;
};

[[nodiscard]] DataPaths resolveDataPaths();
[[nodiscard]] bool readUtf8File(const std::filesystem::path& path, std::string& contents);
[[nodiscard]] bool atomicWriteUtf8(const std::filesystem::path& path,
                                   const std::string& contents,
                                   std::wstring* error = nullptr);
[[nodiscard]] bool quarantineCorruptFile(const std::filesystem::path& path,
                                         std::filesystem::path* renamedTo = nullptr);

struct LoadedData {
    core::Settings settings;
    core::ExposureStore exposure;
    bool settingsRecovered{false};
    bool exposureRecovered{false};
};

[[nodiscard]] LoadedData loadData(const DataPaths& paths);
[[nodiscard]] bool saveSettings(const DataPaths& paths, const core::Settings& settings);
[[nodiscard]] bool saveExposure(const DataPaths& paths, const core::ExposureStore& exposure);

} // namespace aoc::platform

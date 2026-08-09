#include "aoc/platform/persistence.h"

#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <fstream>

namespace aoc::platform {

DataPaths resolveDataPaths() {
    PWSTR localAppData = nullptr;
    DataPaths paths;
    // An explicit LOCALAPPDATA is useful for portable smoke tests and keeps those runs out of the user's profile.
    wchar_t environmentPath[MAX_PATH * 4]{};
    const DWORD environmentLength = GetEnvironmentVariableW(L"LOCALAPPDATA", environmentPath,
                                                              static_cast<DWORD>(std::size(environmentPath)));
    if (environmentLength > 0 && environmentLength < std::size(environmentPath)) {
        paths.root = std::filesystem::path(environmentPath) / L"AdaptiveOledClockCpp";
    } else if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &localAppData))) {
        paths.root = std::filesystem::path(localAppData) / L"AdaptiveOledClockCpp";
        CoTaskMemFree(localAppData);
    } else {
        wchar_t buffer[MAX_PATH]{};
        GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
        paths.root = std::filesystem::path(buffer) / L"AdaptiveOledClockCpp";
    }
    paths.settingsFile = paths.root / L"settings.conf";
    paths.exposureFile = paths.root / L"exposure.dat";
    paths.logFile = paths.root / L"app.log";
    return paths;
}

bool readUtf8File(const std::filesystem::path& path, std::string& contents) {
    constexpr std::uintmax_t kMaximumDataFileBytes = 8U * 1024U * 1024U;
    std::error_code sizeError;
    const std::uintmax_t size = std::filesystem::file_size(path, sizeError);
    if (sizeError || size > kMaximumDataFileBytes) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    contents.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return true;
}

bool atomicWriteUtf8(const std::filesystem::path& path,
                     const std::string& contents,
                     std::wstring* error) {
    try {
        std::filesystem::create_directories(path.parent_path());
    } catch (...) {
        if (error) *error = L"could not create the application data directory";
        return false;
    }
    const std::filesystem::path temporary = path.wstring() + L".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            if (error) *error = L"could not open temporary file";
            return false;
        }
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        output.flush();
        if (!output) {
            if (error) *error = L"could not write temporary file";
            return false;
        }
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        if (error) *error = L"atomic replace failed with Win32 error " + std::to_wstring(GetLastError());
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

bool quarantineCorruptFile(const std::filesystem::path& path, std::filesystem::path* renamedTo) {
    if (!std::filesystem::exists(path)) return false;
    SYSTEMTIME now{};
    GetLocalTime(&now);
    const std::wstring suffix = L".corrupt-" + std::to_wstring(now.wYear) +
                                std::to_wstring(now.wMonth) + std::to_wstring(now.wDay) + L"-" +
                                std::to_wstring(now.wHour) + std::to_wstring(now.wMinute) +
                                std::to_wstring(now.wSecond) + std::to_wstring(now.wMilliseconds);
    std::filesystem::path destination = path.wstring() + suffix;
    if (MoveFileExW(path.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH) == FALSE) return false;
    if (renamedTo) *renamedTo = destination;
    return true;
}

LoadedData loadData(const DataPaths& paths) {
    LoadedData data;
    data.settings = core::Settings::defaults();
    std::string contents;
    if (readUtf8File(paths.settingsFile, contents)) {
        const auto result = core::deserializeSettings(contents);
        data.settings = result.value;
        data.settingsRecovered = result.recovered;
        data.settingsMigrated = result.migrated;
        if (result.recovered && !result.migrated) (void)quarantineCorruptFile(paths.settingsFile);
    }
    if (readUtf8File(paths.exposureFile, contents)) {
        const auto result = core::deserializeExposure(contents);
        data.exposure = result.value;
        data.exposureRecovered = result.recovered;
        if (result.recovered) (void)quarantineCorruptFile(paths.exposureFile);
    }
    return data;
}

bool saveSettings(const DataPaths& paths, const core::Settings& settings) {
    return atomicWriteUtf8(paths.settingsFile, core::serializeSettings(settings));
}

bool saveExposure(const DataPaths& paths, const core::ExposureStore& exposure) {
    return atomicWriteUtf8(paths.exposureFile, core::serializeExposure(exposure));
}

} // namespace aoc::platform

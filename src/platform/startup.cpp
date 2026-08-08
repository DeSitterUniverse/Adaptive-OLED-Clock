#include "aoc/platform/startup.h"

#include <windows.h>

namespace aoc::platform {
namespace {
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"AdaptiveOledClockCpp";
}

bool setLaunchAtStartup(bool enabled, const std::wstring& executablePath) {
    HKEY key = nullptr;
    const REGSAM access = KEY_QUERY_VALUE | KEY_SET_VALUE;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, access, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    bool success = true;
    if (enabled) {
        std::wstring command = L"\"" + executablePath + L"\"";
        success = RegSetValueExW(key, kValueName, 0, REG_SZ,
                                 reinterpret_cast<const BYTE*>(command.c_str()),
                                 static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    } else {
        const LONG result = RegDeleteValueW(key, kValueName);
        success = result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
    }
    RegCloseKey(key);
    return success;
}

bool launchAtStartupEnabled(const std::wstring& executablePath) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    wchar_t value[2048]{};
    DWORD type = 0;
    DWORD bytes = sizeof(value);
    const bool found = RegQueryValueExW(key, kValueName, nullptr, &type, reinterpret_cast<BYTE*>(value), &bytes) == ERROR_SUCCESS && type == REG_SZ;
    RegCloseKey(key);
    if (!found) return false;
    const std::wstring expected = L"\"" + executablePath + L"\"";
    return std::wstring(value) == expected;
}

} // namespace aoc::platform

#include "aoc/platform/logging.h"

#include <windows.h>

#include <fstream>

namespace aoc::platform {
namespace {

std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

} // namespace

Logger::Logger(std::filesystem::path path) : path_(std::move(path)) {
    try {
        std::filesystem::create_directories(path_.parent_path());
    } catch (...) {
    }
}

void Logger::info(const std::wstring& message) { write("INFO", message); }
void Logger::warning(const std::wstring& message) { write("WARN", message); }
void Logger::error(const std::wstring& message) { write("ERROR", message); }

void Logger::write(const char* level, const std::wstring& message) {
    std::scoped_lock lock(mutex_);
    std::ofstream output(path_, std::ios::app | std::ios::binary);
    if (!output) return;
    SYSTEMTIME now{};
    GetLocalTime(&now);
    output << now.wYear << '-' << (now.wMonth < 10 ? "0" : "") << now.wMonth << '-'
           << (now.wDay < 10 ? "0" : "") << now.wDay << ' '
           << (now.wHour < 10 ? "0" : "") << now.wHour << ':'
           << (now.wMinute < 10 ? "0" : "") << now.wMinute << ':'
           << (now.wSecond < 10 ? "0" : "") << now.wSecond << ' '
           << level << ' ' << utf8(message) << "\r\n";
}

} // namespace aoc::platform

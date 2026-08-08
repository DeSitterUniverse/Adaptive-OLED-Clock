#pragma once

#include <filesystem>
#include <mutex>
#include <string>

namespace aoc::platform {

class Logger {
public:
    explicit Logger(std::filesystem::path path);
    void info(const std::wstring& message);
    void warning(const std::wstring& message);
    void error(const std::wstring& message);

private:
    void write(const char* level, const std::wstring& message);
    std::filesystem::path path_;
    std::mutex mutex_;
};

} // namespace aoc::platform

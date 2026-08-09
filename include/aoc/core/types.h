#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace aoc::core {

struct Color {
    std::uint8_t r{176};
    std::uint8_t g{176};
    std::uint8_t b{176};
    std::uint8_t a{255};

    friend bool operator==(const Color&, const Color&) = default;
};

struct PointD {
    double x{0.0};
    double y{0.0};
};

struct SizeD {
    double width{0.0};
    double height{0.0};
};

struct RectD {
    double left{0.0};
    double top{0.0};
    double right{0.0};
    double bottom{0.0};

    [[nodiscard]] double width() const noexcept { return right - left; }
    [[nodiscard]] double height() const noexcept { return bottom - top; }
    [[nodiscard]] double area() const noexcept;
    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] PointD center() const noexcept { return {(left + right) / 2.0, (top + bottom) / 2.0}; }
    [[nodiscard]] bool contains(const RectD& other) const noexcept;
    [[nodiscard]] bool contains(PointD point) const noexcept;
    [[nodiscard]] bool intersects(const RectD& other) const noexcept;
    [[nodiscard]] RectD intersection(const RectD& other) const noexcept;
};

struct RectI {
    int left{0};
    int top{0};
    int right{0};
    int bottom{0};

    [[nodiscard]] int width() const noexcept { return right - left; }
    [[nodiscard]] int height() const noexcept { return bottom - top; }
    [[nodiscard]] bool isValid() const noexcept { return right > left && bottom > top; }
    [[nodiscard]] PointD center() const noexcept { return {(left + right) / 2.0, (top + bottom) / 2.0}; }
    [[nodiscard]] bool contains(const RectI& other) const noexcept;
    [[nodiscard]] bool intersects(const RectI& other) const noexcept;
    [[nodiscard]] RectI intersection(const RectI& other) const noexcept;

    friend bool operator==(const RectI&, const RectI&) = default;
};

struct NormalizedRect {
    double left{0.0};
    double top{0.0};
    double right{1.0};
    double bottom{1.0};

    [[nodiscard]] double width() const noexcept { return right - left; }
    [[nodiscard]] double height() const noexcept { return bottom - top; }
    [[nodiscard]] double area() const noexcept;
    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] PointD center() const noexcept { return {(left + right) / 2.0, (top + bottom) / 2.0}; }
    [[nodiscard]] NormalizedRect intersection(const NormalizedRect& other) const noexcept;

    friend bool operator==(const NormalizedRect&, const NormalizedRect&) = default;
};

struct NormalizedPoint {
    double x{0.5};
    double y{0.5};

    friend bool operator==(const NormalizedPoint&, const NormalizedPoint&) = default;
};

enum class TimeFormat : std::uint8_t {
    Locale = 0,
    TwelveHour = 1,
    TwentyFourHour = 2,
};

enum class MovementMode : std::uint8_t {
    WholeScreen = 0,
    LocalWander = 1,
    EdgeOnly = 2,
};

enum class FontWeight : std::uint8_t {
    Normal = 0,
    SemiBold = 1,
};

enum class MonitorMode : std::uint8_t {
    FollowPrimary = 0,
    Fixed = 1,
};

struct MonitorInfo {
    std::string stableKey;
    std::wstring displayName;
    RectI boundsPx;
    RectI workAreaPx;
    std::uint32_t dpiX{96};
    std::uint32_t dpiY{96};
    bool primary{false};
    bool displayOn{true};
};

constexpr std::size_t kExposureColumns = 12;
constexpr std::size_t kExposureRows = 8;
constexpr std::size_t kExposureCellCount = kExposureColumns * kExposureRows;

} // namespace aoc::core

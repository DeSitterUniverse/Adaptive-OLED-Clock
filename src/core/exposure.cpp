#include "aoc/core/exposure.h"

#include "aoc/core/geometry.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace aoc::core {
namespace {

std::string escapeKey(const std::string& key) {
    std::ostringstream output;
    output << std::uppercase << std::hex;
    for (const unsigned char character : key) {
        if (character == '%' || character == '=' || character == '\n' || character == '\r') {
            output << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(character);
        } else {
            output << static_cast<char>(character);
        }
    }
    return output.str();
}

std::string unescapeKey(const std::string& value) {
    std::string output;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '%' && index + 2 < value.size()) {
            try {
                output.push_back(static_cast<char>(std::stoi(value.substr(index + 1, 2), nullptr, 16)));
                index += 2;
                continue;
            } catch (...) {
            }
        }
        output.push_back(value[index]);
    }
    return output;
}

bool parseDouble(const std::string& text, double& value) {
    try {
        std::size_t consumed = 0;
        value = std::stod(text, &consumed);
        return consumed == text.size() && std::isfinite(value) && value >= 0.0;
    } catch (...) {
        return false;
    }
}

} // namespace

void ExposureMap::charge(const NormalizedRect& renderedBounds, double elapsedSeconds) noexcept {
    const NormalizedRect bounds = clampNormalizedRect(renderedBounds);
    if (!bounds.isValid() || !std::isfinite(elapsedSeconds) || elapsedSeconds <= 0.0) return;
    const double renderedArea = bounds.area();
    if (renderedArea <= std::numeric_limits<double>::epsilon()) return;
    for (std::size_t row = 0; row < kExposureRows; ++row) {
        for (std::size_t column = 0; column < kExposureColumns; ++column) {
            const NormalizedRect cellBounds{
                static_cast<double>(column) / kExposureColumns,
                static_cast<double>(row) / kExposureRows,
                static_cast<double>(column + 1) / kExposureColumns,
                static_cast<double>(row + 1) / kExposureRows,
            };
            const double overlap = bounds.intersection(cellBounds).area();
            seconds[row * kExposureColumns + column] += elapsedSeconds * overlap / renderedArea;
        }
    }
}

double ExposureMap::weightedExposure(const NormalizedRect& renderedBounds) const noexcept {
    const NormalizedRect bounds = clampNormalizedRect(renderedBounds);
    if (!bounds.isValid() || bounds.area() <= std::numeric_limits<double>::epsilon()) return 0.0;
    double result = 0.0;
    for (std::size_t row = 0; row < kExposureRows; ++row) {
        for (std::size_t column = 0; column < kExposureColumns; ++column) {
            const NormalizedRect cellBounds{
                static_cast<double>(column) / kExposureColumns,
                static_cast<double>(row) / kExposureRows,
                static_cast<double>(column + 1) / kExposureColumns,
                static_cast<double>(row + 1) / kExposureRows,
            };
            const double cellArea = cellBounds.area();
            result += seconds[row * kExposureColumns + column] *
                      (cellArea <= std::numeric_limits<double>::epsilon()
                           ? 0.0
                           : bounds.intersection(cellBounds).area() / cellArea);
        }
    }
    return result;
}

double ExposureMap::totalSeconds() const noexcept {
    double total = 0.0;
    for (const double value : seconds) total += value;
    return total;
}

double ExposureMap::cell(std::size_t column, std::size_t row) const noexcept {
    if (column >= kExposureColumns || row >= kExposureRows) return 0.0;
    return seconds[row * kExposureColumns + column];
}

std::pair<std::size_t, std::size_t> ExposureMap::leastExposedCell() const noexcept {
    const auto found = std::min_element(seconds.begin(), seconds.end());
    const std::size_t index = static_cast<std::size_t>(std::distance(seconds.begin(), found));
    return {index % kExposureColumns, index / kExposureColumns};
}

std::pair<std::size_t, std::size_t> ExposureMap::mostExposedCell() const noexcept {
    const auto found = std::max_element(seconds.begin(), seconds.end());
    const std::size_t index = static_cast<std::size_t>(std::distance(seconds.begin(), found));
    return {index % kExposureColumns, index / kExposureColumns};
}

double ExposureMap::imbalance() const noexcept {
    if (totalSeconds() <= std::numeric_limits<double>::epsilon()) return 0.0;
    const auto [minimum, maximum] = std::minmax_element(seconds.begin(), seconds.end());
    if (*maximum <= std::numeric_limits<double>::epsilon()) return 0.0;
    return (*maximum - *minimum) / *maximum;
}

ExposureMap& ExposureStore::forMonitor(const std::string& stableKey) {
    return maps_[stableKey];
}

const ExposureMap* ExposureStore::find(const std::string& stableKey) const noexcept {
    const auto found = maps_.find(stableKey);
    return found == maps_.end() ? nullptr : &found->second;
}

void ExposureTracker::settleInternal(std::chrono::steady_clock::time_point now) {
    if (!initialized_) return;
    if (active_ && now > lastSettlement_ && !monitorKey_.empty()) {
        const double elapsed = std::chrono::duration<double>(now - lastSettlement_).count();
        store_.forMonitor(monitorKey_).charge(renderedBounds_, elapsed);
    }
    lastSettlement_ = now;
}

void ExposureTracker::setState(const std::string& monitorKey,
                               NormalizedRect renderedBounds,
                               bool effectivelyVisible,
                               std::chrono::steady_clock::time_point now) {
    if (initialized_) settleInternal(now);
    monitorKey_ = monitorKey;
    renderedBounds_ = clampNormalizedRect(renderedBounds);
    active_ = effectivelyVisible && renderedBounds_.isValid() && !monitorKey_.empty();
    initialized_ = true;
    lastSettlement_ = now;
}

void ExposureTracker::checkpoint(std::chrono::steady_clock::time_point now) {
    settleInternal(now);
}

void ExposureTracker::settle(std::chrono::steady_clock::time_point now) {
    settleInternal(now);
    active_ = false;
}

std::string serializeExposure(const ExposureStore& store) {
    std::ostringstream output;
    output << "# Adaptive OLED Clock C++ exposure store\nversion=1\n";
    std::size_t index = 0;
    output << std::setprecision(17);
    for (const auto& [key, map] : store.maps()) {
        output << "monitor." << index << ".key=" << escapeKey(key) << '\n';
        output << "monitor." << index << ".cells=";
        for (std::size_t cell = 0; cell < kExposureCellCount; ++cell) {
            if (cell != 0) output << ',';
            output << map.seconds[cell];
        }
        output << '\n';
        ++index;
    }
    output << "monitorCount=" << index << '\n';
    return output.str();
}

ExposureLoadResult deserializeExposure(const std::string& text) {
    ExposureLoadResult result;
    std::map<std::string, std::string> properties;
    std::stringstream input(text);
    std::string line;
    bool syntaxOk = true;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            syntaxOk = false;
            continue;
        }
        properties[line.substr(0, separator)] = line.substr(separator + 1);
    }
    int version = 0;
    try {
        version = std::stoi(properties.at("version"));
    } catch (...) {
        syntaxOk = false;
    }
    int count = 0;
    try {
        count = std::stoi(properties.at("monitorCount"));
    } catch (...) {
        syntaxOk = false;
    }
    if (!syntaxOk || version != 1 || count < 0 || count > 1000) {
        result.recovered = true;
        result.error = version == 1 ? "exposure file was malformed" : "unsupported exposure version";
        return result;
    }
    std::map<std::string, ExposureMap> maps;
    for (int index = 0; index < count; ++index) {
        try {
            const std::string key = unescapeKey(properties.at("monitor." + std::to_string(index) + ".key"));
            const std::string cells = properties.at("monitor." + std::to_string(index) + ".cells");
            std::stringstream values(cells);
            ExposureMap map;
            std::string item;
            std::size_t cell = 0;
            while (std::getline(values, item, ',')) {
                if (cell >= kExposureCellCount || !parseDouble(item, map.seconds[cell])) throw std::runtime_error("invalid exposure cell");
                ++cell;
            }
            if (key.empty() || cell != kExposureCellCount) throw std::runtime_error("invalid exposure map");
            maps[key] = map;
        } catch (...) {
            result.recovered = true;
            result.error = "exposure file contained an invalid map";
            return result;
        }
    }
    result.value.replace(std::move(maps));
    return result;
}

} // namespace aoc::core

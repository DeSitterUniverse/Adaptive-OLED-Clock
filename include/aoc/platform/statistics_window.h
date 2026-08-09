#pragma once

#include "aoc/core/exposure.h"
#include "aoc/core/types.h"
#include "aoc/platform/win32_raii.h"

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace aoc::platform {

class StatisticsWindow {
public:
    using SimpleCallback = std::function<void()>;

    StatisticsWindow() = default;
    ~StatisticsWindow();

    StatisticsWindow(const StatisticsWindow&) = delete;
    StatisticsWindow& operator=(const StatisticsWindow&) = delete;

    [[nodiscard]] bool create(HINSTANCE instance, HWND owner, SimpleCallback onReset,
                              SimpleCallback onExport);
    void show(const std::vector<core::MonitorInfo>& monitors,
              const core::ExposureStore& exposure,
              const std::string& selectedKey,
              core::MovementMode mode);
    void refresh(const std::vector<core::MonitorInfo>& monitors,
                 const core::ExposureStore& exposure,
                 const std::string& selectedKey,
                 core::MovementMode mode);
    void hide();
    [[nodiscard]] bool visible() const noexcept { return hwnd_ && IsWindowVisible(hwnd_) != FALSE; }
    [[nodiscard]] HWND hwnd() const noexcept { return hwnd_; }

private:
    static LRESULT CALLBACK windowProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void layoutControls(int width, int height);
    void syncMonitorCombo();
    void updateSummary();
    void paintHeatmap(HDC dc, const RECT& clientRect);
    [[nodiscard]] int scale(int value) const noexcept;

    HINSTANCE instance_{nullptr};
    HWND owner_{nullptr};
    HWND hwnd_{nullptr};
    UINT dpi_{96};
    UniqueGdiFont controlFont_;
    std::vector<HWND> controls_;
    HWND monitorCombo_{nullptr};
    HWND totalLabel_{nullptr};
    HWND leastLabel_{nullptr};
    HWND imbalanceLabel_{nullptr};
    HWND modeLabel_{nullptr};
    HWND resetButton_{nullptr};
    HWND exportButton_{nullptr};
    HWND closeButton_{nullptr};
    SimpleCallback onReset_;
    SimpleCallback onExport_;
    std::vector<core::MonitorInfo> monitors_;
    core::ExposureStore exposure_;
    std::string selectedKey_;
    core::MovementMode mode_{core::MovementMode::EdgeOnly};
    RECT heatmapRect_{};
};

} // namespace aoc::platform

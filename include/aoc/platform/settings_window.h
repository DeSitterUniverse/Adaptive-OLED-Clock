#pragma once

#include "aoc/core/settings.h"
#include "aoc/core/types.h"

#include <windows.h>

#include <functional>
#include <vector>

namespace aoc::platform {

class SettingsWindow {
public:
    using ApplyCallback = std::function<void(const core::Settings&)>;
    using SimpleCallback = std::function<void()>;

    SettingsWindow() = default;
    ~SettingsWindow();

    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    [[nodiscard]] bool create(HINSTANCE instance, HWND owner, ApplyCallback onApply,
                              SimpleCallback onReset, SimpleCallback onPreset,
                              SimpleCallback onPositioning);
    void show(const core::Settings& settings, const std::vector<core::MonitorInfo>& monitors);
    void hide();
    [[nodiscard]] bool visible() const noexcept { return hwnd_ != nullptr && IsWindowVisible(hwnd_) != FALSE; }
    [[nodiscard]] HWND hwnd() const noexcept { return hwnd_; }

private:
    static LRESULT CALLBACK windowProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void createControls();
    void layoutControls(int width, int height);
    void syncToControls();
    void applyFromControls();
    void addExcludedArea();
    void removeSelectedExcludedArea();
    void clearExcludedAreas();
    void updateExcludedList();
    void chooseTextColor();
    void addLabel(const wchar_t* text, int x, int y, int width, int height);
    HWND addControl(DWORD style, const wchar_t* className, const wchar_t* text,
                    int id, int x, int y, int width, int height, DWORD exStyle = 0);

    HINSTANCE instance_{nullptr};
    HWND owner_{nullptr};
    HWND hwnd_{nullptr};
    ApplyCallback onApply_;
    SimpleCallback onReset_;
    SimpleCallback onPreset_;
    SimpleCallback onPositioning_;
    core::Settings settings_{};
    std::vector<core::MonitorInfo> monitors_;
    bool syncing_{false};
    std::vector<HWND> labels_;
    std::vector<HWND> controls_;

    HWND timeFormat_{nullptr};
    HWND showAmPm_{nullptr};
    HWND fontSize_{nullptr};
    HWND colorButton_{nullptr};
    HWND opacity_{nullptr};
    HWND interval_{nullptr};
    HWND movementMode_{nullptr};
    HWND allowedLeft_{nullptr};
    HWND allowedTop_{nullptr};
    HWND allowedRight_{nullptr};
    HWND allowedBottom_{nullptr};
    HWND excludedLeft_{nullptr};
    HWND excludedTop_{nullptr};
    HWND excludedRight_{nullptr};
    HWND excludedBottom_{nullptr};
    HWND excludedList_{nullptr};
    HWND edgeMargin_{nullptr};
    HWND monitorMode_{nullptr};
    HWND fixedMonitor_{nullptr};
    HWND preferredX_{nullptr};
    HWND preferredY_{nullptr};
    HWND fullscreen_{nullptr};
    HWND startup_{nullptr};
    HWND hotkey_{nullptr};
    HWND boostOpacity_{nullptr};
    HWND boostDuration_{nullptr};
};

} // namespace aoc::platform

#pragma once

#include "aoc/core/settings.h"
#include "aoc/core/types.h"

#include <windows.h>

#include <array>
#include <functional>
#include <vector>

namespace aoc::platform {

class SettingsWindow {
public:
    using ApplyCallback = std::function<void(const core::Settings&, bool committed)>;
    using SimpleCallback = std::function<void()>;

    SettingsWindow() = default;
    ~SettingsWindow();

    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    [[nodiscard]] bool create(HINSTANCE instance, HWND owner, ApplyCallback onApply,
                              SimpleCallback onReset, SimpleCallback onPreset,
                              SimpleCallback onPositioning, SimpleCallback onStatistics);
    void show(const core::Settings& settings, const std::vector<core::MonitorInfo>& monitors);
    void hide();
    [[nodiscard]] bool visible() const noexcept { return hwnd_ != nullptr && IsWindowVisible(hwnd_) != FALSE; }
    [[nodiscard]] HWND hwnd() const noexcept { return hwnd_; }

private:
    static LRESULT CALLBACK windowProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void createControls();
    void layoutControls(int width, int height);
    void setActiveTab(int tab);
    void syncToControls();
    void applyFromControls(bool committed = true);
    void addExcludedArea();
    void removeSelectedExcludedArea();
    void clearExcludedAreas();
    void updateExcludedList();
    void chooseTextColor();
    void updateSliderLabels();
    void updateAllowedAreaEditorState();
    void scrollBy(int amount);
    void setScrollOffset(int offset);
    void schedulePreviewApply();
    static LRESULT CALLBACK pageControlSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    [[nodiscard]] int scale(int value) const noexcept;
    [[nodiscard]] HWND addControl(int page, DWORD style, const wchar_t* className,
                                  const wchar_t* text, int id, int x = 0, int y = 0,
                                  int width = 0, int height = 0, DWORD exStyle = 0);
    void addLabel(int page, const wchar_t* text);
    void addPageControl(int page, HWND control);

    HINSTANCE instance_{nullptr};
    HWND owner_{nullptr};
    HWND hwnd_{nullptr};
    ApplyCallback onApply_;
    SimpleCallback onReset_;
    SimpleCallback onPreset_;
    SimpleCallback onPositioning_;
    SimpleCallback onStatistics_;
    core::Settings settings_{};
    std::vector<core::MonitorInfo> monitors_;
    bool syncing_{false};
    int activeTab_{0};
    UINT dpi_{96};
    HFONT controlFont_{nullptr};
    std::array<std::vector<HWND>, 4> pageControls_;

    HWND tabs_{nullptr};
    HWND pageHost_{nullptr};
    HWND timeFormat_{nullptr};
    HWND showAmPm_{nullptr};
    HWND showSeconds_{nullptr};
    HWND showDate_{nullptr};
    HWND fontFamily_{nullptr};
    HWND fontWeight_{nullptr};
    HWND fontSize_{nullptr};
    HWND fontSizeValue_{nullptr};
    HWND colorButton_{nullptr};
    HWND opacity_{nullptr};
    HWND opacityValue_{nullptr};
    HWND boostOpacity_{nullptr};
    HWND boostOpacityValue_{nullptr};
    HWND boostDuration_{nullptr};

    HWND movementMode_{nullptr};
    HWND interval_{nullptr};
    HWND microShiftEnabled_{nullptr};
    HWND microShiftRadius_{nullptr};
    HWND microShiftRadiusValue_{nullptr};
    HWND edgeMargin_{nullptr};
    HWND edgeMarginValue_{nullptr};
    HWND allowedPreset_{nullptr};
    HWND allowedAreaHelp_{nullptr};
    HWND allowedLeftLabel_{nullptr};
    HWND allowedTopLabel_{nullptr};
    HWND allowedRightLabel_{nullptr};
    HWND allowedBottomLabel_{nullptr};
    HWND allowedLeft_{nullptr};
    HWND allowedTop_{nullptr};
    HWND allowedRight_{nullptr};
    HWND allowedBottom_{nullptr};
    HWND preferredEnabled_{nullptr};
    HWND preferredSummary_{nullptr};
    HWND excludedLeft_{nullptr};
    HWND excludedTop_{nullptr};
    HWND excludedRight_{nullptr};
    HWND excludedBottom_{nullptr};
    HWND excludedList_{nullptr};
    HWND excludedAdd_{nullptr};
    HWND excludedRemove_{nullptr};
    HWND excludedClear_{nullptr};

    HWND monitorMode_{nullptr};
    HWND fullscreen_{nullptr};
    HWND startup_{nullptr};
    HWND hotkey_{nullptr};

    HWND statisticsButton_{nullptr};
    HWND resetButton_{nullptr};
    HWND presetButton_{nullptr};
    HWND positioningButton_{nullptr};
    HWND closeButton_{nullptr};

    int verticalOffset_{0};
    int scrollMaximum_{0};
    bool sliderTracking_{false};
};

} // namespace aoc::platform

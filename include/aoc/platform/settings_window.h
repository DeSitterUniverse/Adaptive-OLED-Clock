#pragma once

#include "aoc/core/settings.h"
#include "aoc/core/types.h"
#include "aoc/platform/win32_raii.h"

#include <windows.h>

#include <array>
#include <functional>
#include <string>
#include <unordered_set>
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
                              SimpleCallback onPositioning, SimpleCallback onStatistics);
    void show(const core::Settings& settings, const std::vector<core::MonitorInfo>& monitors);
    void syncApplied(const core::Settings& settings,
                     const std::vector<core::MonitorInfo>& monitors,
                     bool force);
    void hide();
    [[nodiscard]] bool visible() const noexcept { return hwnd_ != nullptr && IsWindowVisible(hwnd_) != FALSE; }
    [[nodiscard]] HWND hwnd() const noexcept { return hwnd_; }

private:
    static LRESULT CALLBACK windowProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    bool createControls();
    bool createClockPage();
    bool createMovementPage();
    bool createDisplayPage();
    void layoutControls(int width, int height);
    void setActiveTab(int tab);
    void syncToControls();
    void syncClockPage();
    void syncMovementPage();
    void syncDisplayPage();
    void readClockPage(core::Settings& next) const;
    void readMovementPage(core::Settings& next) const;
    void readDisplayPage(core::Settings& next) const;
    void applyFromControls(bool committed = true);
    void chooseTextColor();
    void updateMovementEditorState();
    void loadInstalledFonts();
    void markPendingEdit(HWND control);
    void clearPendingEdits();
    void scrollBy(int amount);
    void setScrollOffset(int offset);
    void applyVisualTheme();
    void drawButton(const DRAWITEMSTRUCT& item) const;
    void drawTab(const DRAWITEMSTRUCT& item) const;
    void paintTabs(HDC dc) const;
    static LRESULT CALLBACK pageControlSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    static LRESULT CALLBACK tabControlSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    [[nodiscard]] int scale(int value) const noexcept;
    [[nodiscard]] HWND addControl(int page, DWORD style, const wchar_t* className,
                                  const wchar_t* text, int id, int x = 0, int y = 0,
                                  int width = 0, int height = 0, DWORD exStyle = 0);
    [[nodiscard]] HWND addLabel(int page, const wchar_t* text);
    void addPageControl(int page, HWND control);

    struct PageControls {
        std::vector<HWND> controls;
    };

    HINSTANCE instance_{nullptr};
    HWND owner_{nullptr};
    HWND hwnd_{nullptr};
    ApplyCallback onApply_;
    SimpleCallback onPositioning_;
    SimpleCallback onStatistics_;
    core::Settings settings_{};
    std::vector<core::MonitorInfo> monitors_;
    bool syncing_{false};
    int activeTab_{0};
    UINT dpi_{96};
    UniqueGdiFont controlFont_;
    UniqueGdiFont titleFont_;
    std::array<PageControls, 3> pages_;
    std::vector<HWND> allControls_;
    bool controlCreationFailed_{false};

    HWND tabs_{nullptr};
    HWND pageHost_{nullptr};
    HWND titleLabel_{nullptr};
    HWND subtitleLabel_{nullptr};
    HWND timeFormatLabel_{nullptr};
    HWND timeFormat_{nullptr};
    HWND showAmPm_{nullptr};
    HWND showSeconds_{nullptr};
    HWND showDate_{nullptr};
    HWND fontFamily_{nullptr};
    HWND fontFamilyLabel_{nullptr};
    HWND fontWeight_{nullptr};
    HWND fontWeightLabel_{nullptr};
    HWND fontSize_{nullptr};
    HWND fontSizeLabel_{nullptr};
    HWND fontSizeValue_{nullptr};
    HWND colorButton_{nullptr};
    HWND colorLabel_{nullptr};
    HWND opacity_{nullptr};
    HWND opacityLabel_{nullptr};
    HWND opacityValue_{nullptr};
    HWND boostOpacity_{nullptr};
    HWND boostOpacityLabel_{nullptr};
    HWND boostOpacityValue_{nullptr};
    HWND boostDuration_{nullptr};
    HWND boostDurationLabel_{nullptr};

    HWND movementMode_{nullptr};
    HWND movementModeLabel_{nullptr};
    HWND localAreaRadius_{nullptr};
    HWND localAreaRadiusLabel_{nullptr};
    HWND localAreaHelp_{nullptr};
    HWND intervalHours_{nullptr};
    HWND intervalMinutes_{nullptr};
    HWND intervalSeconds_{nullptr};
    HWND intervalLabel_{nullptr};
    HWND intervalHoursLabel_{nullptr};
    HWND intervalMinutesLabel_{nullptr};
    HWND intervalSecondsLabel_{nullptr};
    HWND microShiftEnabled_{nullptr};
    HWND microShiftCount_{nullptr};
    HWND microShiftCountLabel_{nullptr};
    HWND microShiftDistance_{nullptr};
    HWND microShiftDistanceLabel_{nullptr};
    HWND edgeMargin_{nullptr};
    HWND edgeMarginLabel_{nullptr};
    HWND allowedPreset_{nullptr};
    HWND allowedPresetLabel_{nullptr};
    HWND allowedAreaHelp_{nullptr};
    HWND allowedLeftLabel_{nullptr};
    HWND allowedTopLabel_{nullptr};
    HWND allowedRightLabel_{nullptr};
    HWND allowedBottomLabel_{nullptr};
    HWND allowedLeft_{nullptr};
    HWND allowedTop_{nullptr};
    HWND allowedRight_{nullptr};
    HWND allowedBottom_{nullptr};

    HWND monitorMode_{nullptr};
    HWND monitorModeLabel_{nullptr};
    HWND fullscreen_{nullptr};
    HWND startup_{nullptr};
    HWND hotkey_{nullptr};
    HWND displayHelp_{nullptr};

    HWND statisticsButton_{nullptr};
    HWND statusLabel_{nullptr};
    HWND resetButton_{nullptr};
    HWND presetButton_{nullptr};
    HWND positioningButton_{nullptr};
    HWND closeButton_{nullptr};

    int verticalOffset_{0};
    int scrollMaximum_{0};
    int footerTop_{0};
    bool dirty_{false};
    std::vector<std::wstring> installedFonts_;
    std::unordered_set<HWND> pendingEdits_;
};

} // namespace aoc::platform

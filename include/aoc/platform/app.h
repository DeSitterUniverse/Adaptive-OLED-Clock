#pragma once

#include "aoc/core/exposure.h"
#include "aoc/core/clock.h"
#include "aoc/core/settings.h"
#include "aoc/core/types.h"
#include "aoc/core/placement.h"
#include "aoc/platform/logging.h"
#include "aoc/platform/monitor_service.h"
#include "aoc/platform/overlay_window.h"
#include "aoc/platform/persistence.h"
#include "aoc/platform/renderer.h"
#include "aoc/platform/settings_window.h"
#include "aoc/platform/statistics_window.h"

#include <windows.h>
#include <shellapi.h>

#include <chrono>
#include <optional>
#include <random>
#include <vector>

namespace aoc::platform {

class App {
public:
    App(HINSTANCE instance, int showCommand, bool openSettingsOnStart = false);
    ~App();

    [[nodiscard]] int run();

private:
    static LRESULT CALLBACK controllerWindowProc(HWND, UINT, WPARAM, LPARAM);
    static void CALLBACK winEventProc(HWINEVENTHOOK, DWORD, HWND, LONG objectId, LONG childId, DWORD, DWORD);
    LRESULT handleControllerMessage(UINT message, WPARAM wParam, LPARAM lParam);

    [[nodiscard]] bool initialize();
    void shutdown();
    void createControllerWindow();
    void initializeSystemIntegrations();
    void unregisterSystemIntegrations();
    void createTrayIcon();
    void removeTrayIcon();
    void showTrayMenu(POINT screenPoint);
    void handleTrayCommand(UINT command);
    void showStatisticsWindow();
    void refreshOpenStatisticsWindow();
    void resetExposureHistory();
    void exportExposureCsv();
    void refreshMonitorsAndPlacement(bool preservePosition, bool honorPreferredPosition = true);
    void refreshFullscreenState();
    void refreshDisplayState(bool displayOn);
    void refreshTimeAndRender();
    void renderAndPresent();
    void scheduleTimeBoundary();
    void scheduleMicroBoundary();
    void scheduleMajorMove();
    void applyMajorMove();
    void applyMicroShift();
    void setClockVisible(bool visible, const wchar_t* reason);
    void toggleClock();
    void triggerBrightnessBoost();
    void endPositioningMode();
    void beginPositioningMode();
    void handleDraggedPoint(POINT screenPoint);
    void handleDragFinished();
    void rememberMacroPosition(const core::RectI& rect);
    bool persistSettings();
    void persistExposure();
    void applySettings(const core::Settings& settings, bool committed);
    void setStartupRegistration();
    void updateExposureState();
    void logState(const std::wstring& message);
    [[nodiscard]] core::MonitorInfo* selectedMonitor();
    [[nodiscard]] const core::MonitorInfo* selectedMonitor() const;
    [[nodiscard]] RECT currentScreenRect() const;
    [[nodiscard]] double currentOpacity() const noexcept;
    [[nodiscard]] std::wstring currentTimeText() const;
    [[nodiscard]] core::PlacementContext makePlacementContext() const;

    HINSTANCE instance_{nullptr};
    int showCommand_{SW_SHOWNORMAL};
    bool openSettingsOnStart_{false};
    HWND controller_{nullptr};
    HINSTANCE shellInstance_{nullptr};
    UINT trayMessage_{WM_APP + 1};
    NOTIFYICONDATAW trayIcon_{};
    bool trayCreated_{false};
    bool trayFailureLogged_{false};
    bool initialized_{false};
    bool shuttingDown_{false};
    bool clockVisible_{true};
    bool displayOn_{true};
    bool sessionUnlocked_{true};
    bool fullscreen_{false};
    bool positioning_{false};
    bool brightnessBoosted_{false};
    bool comInitialized_{false};
    bool anotherInstance_{false};
    std::chrono::steady_clock::time_point nextMajorMove_{};
    std::uint64_t placementSeed_{0xA0C0C0DEULL};
    std::vector<core::RectI> macroHistory_;
    core::RectI macroAnchorRect_{};
    HANDLE singletonMutex_{nullptr};
    UINT taskbarCreatedMessage_{0};

    DataPaths paths_;
    Logger logger_;
    core::Settings settings_;
    // settings_ may contain an uncommitted live preview. Keep the durable
    // baseline separate so a later commit still detects the net change.
    core::Settings committedSettings_;
    core::ExposureStore exposure_;
    core::ExposureTracker exposureTracker_;
    MonitorService monitorService_;
    std::vector<core::MonitorInfo> monitors_;
    std::optional<core::MonitorInfo> selectedMonitor_;
    core::RectI currentClockRect_{};
    core::SizeD renderedSizeDip_{80.0, 32.0};
    core::SizeD renderedSurfaceDip_{80.0, 32.0};
    core::LocaleHourMode localeHourMode_{core::LocaleHourMode::TwentyFourHour};
    LayeredRenderer renderer_;
    OverlayWindow overlay_;
    SettingsWindow settingsWindow_;
    StatisticsWindow statisticsWindow_;
    HWINEVENTHOOK foregroundHook_{nullptr};
    HWINEVENTHOOK locationChangeHook_{nullptr};
    HPOWERNOTIFY consoleDisplayPower_{nullptr};
    HPOWERNOTIFY monitorPower_{nullptr};
    DWORD sessionNotificationId_{0};
};

} // namespace aoc::platform

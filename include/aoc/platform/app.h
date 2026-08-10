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
#include "aoc/platform/win32_raii.h"

#include <windows.h>
#include <shellapi.h>

#include <chrono>
#include <atomic>
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
    void refreshMonitorsAndPlacement(bool preservePosition);
    void refreshFullscreenState();
    void refreshDisplayState(bool displayOn);
    void refreshTimeAndRender();
    void renderAndPresent();
    void scheduleTimeBoundary();
    void scheduleMajorMove();
    void scheduleMicroShift();
    void restartMovementSchedule();
    void applyMajorMove();
    void applyMicroShift();
    void setClockVisible(bool visible, const wchar_t* reason);
    void toggleClock();
    void triggerBrightnessBoost();
    void endPositioningMode();
    void beginPositioningMode();
    void handleDraggedPoint(POINT screenPoint);
    void handleDragFinished();
    void rememberLocalAnchor();
    void rememberMacroPosition(const core::RectI& rect);
    bool persistSettings();
    void persistExposure();
    void applySettings(const core::Settings& settings);
    [[nodiscard]] bool setStartupRegistration();
    [[nodiscard]] bool armTimer(UINT_PTR id, UINT intervalMilliseconds, const wchar_t* purpose);
    void updateExposureState();
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
    std::atomic_bool fullscreenRefreshPending_{false};
    std::uint64_t placementSeed_{0xA0C0C0DEULL};
    std::uint64_t microShiftStep_{0};
    int nextMicroShiftIndex_{0};
    std::chrono::steady_clock::time_point movementCycleStartedAt_{};
    std::chrono::steady_clock::time_point majorMoveDeadline_{};
    std::vector<core::RectI> macroHistory_;
    core::RectI macroAnchorRect_{};
    UniqueKernelHandle singletonMutex_;
    UINT taskbarCreatedMessage_{0};

    DataPaths paths_;
    Logger logger_;
    core::Settings settings_;
    core::ExposureStore exposure_;
    core::ExposureTracker exposureTracker_;
    MonitorService monitorService_;
    std::vector<core::MonitorInfo> monitors_;
    std::optional<core::MonitorInfo> selectedMonitor_;
    core::RectI currentClockRect_{};
    core::SizeD renderedSizeDip_{80.0, 32.0};
    core::SizeD renderedSurfaceDip_{80.0, 32.0};
    core::LocaleHourMode localeHourMode_{core::LocaleHourMode::TwentyFourHour};
    std::wstring lastRenderedTimeText_;
    LayeredRenderer renderer_;
    OverlayWindow overlay_;
    SettingsWindow settingsWindow_;
    StatisticsWindow statisticsWindow_;
    UniqueWinEventHook foregroundHook_;
    UniqueWinEventHook locationChangeHook_;
    UniquePowerNotification consoleDisplayPower_;
    UniquePowerNotification monitorPower_;
    DWORD sessionNotificationId_{0};
};

} // namespace aoc::platform

#include "aoc/platform/app.h"

#include "aoc/core/fullscreen.h"
#include "aoc/core/geometry.h"
#include "aoc/core/monitor.h"
#include "aoc/core/placement.h"
#include "aoc/platform/fullscreen_service.h"
#include "aoc/platform/startup.h"

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <powerbase.h>
#include <shellapi.h>
#include <winnls.h>
#include <wtsapi32.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

namespace aoc::platform {
namespace {

constexpr wchar_t kControllerClass[] = L"AdaptiveOledClockControllerWindow";
constexpr wchar_t kSingleInstanceName[] = L"Local\\AdaptiveOledClockCppSingleInstance";
constexpr int kHotkeyId = 1;
constexpr int kPositioningEscapeHotkeyId = 2;
constexpr UINT_PTR kDisplayTimer = 1;
constexpr UINT_PTR kMajorMoveTimer = 2;
constexpr UINT_PTR kExposureCheckpointTimer = 3;
constexpr UINT_PTR kBoostTimer = 4;
constexpr UINT_PTR kMicroTimer = 5;
constexpr UINT kWinEventMessage = WM_APP + 2;
constexpr UINT kDpiMessage = WM_APP + 3;
constexpr UINT kShowSettingsMessage = WM_APP + 4;
constexpr double kPositioningPaddingDip = 16.0;

constexpr UINT kTrayToggle = 4000;
constexpr UINT kTrayBrightness = 4001;
constexpr UINT kTrayModeEdge = 4002;
constexpr UINT kTrayModeWhole = 4003;
constexpr UINT kTrayModeLocal = 4004;
constexpr UINT kTrayOpacity16 = 4005;
constexpr UINT kTrayOpacity24 = 4006;
constexpr UINT kTrayOpacity32 = 4007;
constexpr UINT kTrayOpacity45 = 4008;
constexpr UINT kTraySettings = 4009;
constexpr UINT kTrayStatistics = 4010;
constexpr UINT kTrayPosition = 4011;
constexpr UINT kTrayExit = 4012;

App* g_eventApp = nullptr;

std::wstring modulePath() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    return path;
}

std::wstring fromUtf8(const std::string& value) {
    if (value.empty()) return {};
    const int required = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) return {};
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), required);
    return result;
}

std::string csvQuote(const std::string& value) {
    std::string result = "\"";
    for (const char character : value) {
        if (character == '"') result += "\"\"";
        else result += character;
    }
    result += '"';
    return result;
}

bool nearlyEqual(double first, double second) { return std::abs(first - second) < 0.01; }

} // namespace

App::App(HINSTANCE instance, int showCommand, bool openSettingsOnStart)
    : instance_(instance),
      showCommand_(showCommand),
      openSettingsOnStart_(openSettingsOnStart),
      paths_(resolveDataPaths()),
      logger_(paths_.logFile),
      settings_(core::Settings::defaults()),
      committedSettings_(settings_),
      exposureTracker_(exposure_) {}

App::~App() { shutdown(); }

int App::run() {
    if (!initialize()) {
        shutdown();
        return anotherInstance_ ? 0 : 1;
    }
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        // Settings and statistics are modeless top-level windows, not dialog
        // resources. IsDialogMessageW supplies Tab/Shift+Tab and default-button
        // navigation without intercepting controller, tray, or overlay messages.
        const auto routeDialogMessage = [&message](HWND dialog) {
            return dialog && IsWindowVisible(dialog) != FALSE &&
                   IsDialogMessageW(dialog, &message) != FALSE;
        };
        if (routeDialogMessage(settingsWindow_.hwnd()) || routeDialogMessage(statisticsWindow_.hwnd())) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    shutdown();
    return static_cast<int>(message.wParam);
}

bool App::initialize() {
    if (initialized_) return true;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    singletonMutex_ = CreateMutexW(nullptr, FALSE, kSingleInstanceName);
    if (!singletonMutex_) {
        logger_.error(L"Could not create the single-instance guard");
        return false;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // A second launch only asks the existing controller to reveal settings; it never loads or writes shared state.
        const HWND existing = FindWindowExW(HWND_MESSAGE, nullptr, kControllerClass, nullptr);
        if (existing) PostMessageW(existing, kShowSettingsMessage, 0, 0);
        anotherInstance_ = true;
        CloseHandle(singletonMutex_);
        singletonMutex_ = nullptr;
        return false;
    }
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    comInitialized_ = SUCCEEDED(comResult);
    INITCOMMONCONTROLSEX commonControls{sizeof(INITCOMMONCONTROLSEX),
                                       ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_TAB_CLASSES};
    InitCommonControlsEx(&commonControls);
    createControllerWindow();
    if (!controller_) return false;

    const LoadedData loaded = loadData(paths_);
    settings_ = loaded.settings;
    committedSettings_ = settings_;
    exposure_ = loaded.exposure;
    clockVisible_ = settings_.clockVisible;
    if (loaded.settingsRecovered) logger_.warning(L"Settings recovery or migration was used");
    if (loaded.exposureRecovered) logger_.warning(L"Exposure recovery/defaults were used");
    if (loaded.settingsMigrated) persistSettings();

    wchar_t pattern[128]{};
    if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_STIMEFORMAT, pattern,
                        static_cast<int>(std::size(pattern))) > 0) {
        localeHourMode_ = core::localeHourModeFromPattern(pattern);
    }
    if (!renderer_.initialize()) {
        logger_.error(L"Direct2D/DirectWrite initialization failed");
        return false;
    }
    if (!overlay_.create(instance_,
                         [this](POINT point) { handleDraggedPoint(point); },
                         [this]() { handleDragFinished(); },
                         [this]() { endPositioningMode(); },
                         [this](UINT) { PostMessageW(controller_, kDpiMessage, 0, 0); })) {
        logger_.error(L"Overlay window creation failed");
        return false;
    }
    if (!settingsWindow_.create(instance_, controller_,
                                [this](const core::Settings& settings, bool committed) { applySettings(settings, committed); },
                                [this]() {
                                    applySettings(core::Settings::defaults(), true);
                                    settingsWindow_.show(settings_, monitors_);
                                },
                                [this]() {
                                    applySettings(core::Settings::oledSafePreset(), true);
                                    settingsWindow_.show(settings_, monitors_);
                                },
                                [this]() { beginPositioningMode(); },
                                [this]() { showStatisticsWindow(); })) {
        logger_.error(L"Settings window creation failed");
        return false;
    }
    if (!statisticsWindow_.create(instance_, controller_,
                                  [this]() { resetExposureHistory(); },
                                  [this]() { exportExposureCsv(); })) {
        logger_.error(L"Statistics window creation failed");
        return false;
    }

    core::SizeD initialText{};
    core::SizeD initialSurface{};
    if (!renderer_.renderText(currentTimeText(), fromUtf8(settings_.fontFamily), settings_.fontWeight,
                              settings_.fontSizeDip, settings_.textColor, currentOpacity(), 96, false,
                              initialText, initialSurface)) {
        logger_.error(L"Initial clock text rendering failed");
        return false;
    }
    renderedSizeDip_ = initialText.width > 0.0 ? initialText : core::SizeD{80.0, 32.0};
    renderedSurfaceDip_ = initialSurface.width > 0.0 ? initialSurface : renderedSizeDip_;
    refreshMonitorsAndPlacement(false);
    initializeSystemIntegrations();
    createTrayIcon();
    setStartupRegistration();
    scheduleTimeBoundary();
    scheduleMicroBoundary();
    scheduleMajorMove();
    SetTimer(controller_, kExposureCheckpointTimer, 60'000, nullptr);
    refreshFullscreenState();
    renderAndPresent();
    initialized_ = true;
    logger_.info(L"Adaptive OLED Clock started");
    if (openSettingsOnStart_) {
        settingsWindow_.show(settings_, monitors_);
        openSettingsOnStart_ = false;
    }
    return true;
}

void App::shutdown() {
    if (shuttingDown_) return;
    shuttingDown_ = true;
    if (initialized_ || controller_) {
        logger_.info(L"Adaptive OLED Clock shutting down");
        exposureTracker_.settle(std::chrono::steady_clock::now());
        persistExposure();
        persistSettings();
        unregisterSystemIntegrations();
        removeTrayIcon();
        statisticsWindow_.hide();
        settingsWindow_.hide();
        overlay_.setInteractive(false);
        overlay_.hide();
        overlay_.destroy();
        if (controller_) DestroyWindow(controller_);
        controller_ = nullptr;
    }
    if (comInitialized_) {
        CoUninitialize();
        comInitialized_ = false;
    }
    if (singletonMutex_) {
        CloseHandle(singletonMutex_);
        singletonMutex_ = nullptr;
    }
    g_eventApp = nullptr;
    initialized_ = false;
}

void App::createControllerWindow() {
    WNDCLASSEXW windowClass{sizeof(WNDCLASSEXW)};
    windowClass.hInstance = instance_;
    windowClass.lpfnWndProc = &App::controllerWindowProc;
    windowClass.lpszClassName = kControllerClass;
    RegisterClassExW(&windowClass);
    controller_ = CreateWindowExW(0, kControllerClass, L"Adaptive OLED Clock Controller",
                                  0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance_, this);
    if (controller_) g_eventApp = this;
}

void App::initializeSystemIntegrations() {
    if (settings_.hotkeyEnabled && RegisterHotKey(controller_, kHotkeyId,
                                                  MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'C') == FALSE) {
        logger_.warning(L"Could not register Ctrl+Alt+C hotkey");
    }
    if (!WTSRegisterSessionNotification(controller_, NOTIFY_FOR_THIS_SESSION)) {
        logger_.warning(L"Could not register session notifications");
    }
    consoleDisplayPower_ = RegisterPowerSettingNotification(controller_, &GUID_CONSOLE_DISPLAY_STATE,
                                                            DEVICE_NOTIFY_WINDOW_HANDLE);
    monitorPower_ = RegisterPowerSettingNotification(controller_, &GUID_MONITOR_POWER_ON,
                                                      DEVICE_NOTIFY_WINDOW_HANDLE);
    foregroundHook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                                       &App::winEventProc, 0, 0,
                                       WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (!foregroundHook_) logger_.warning(L"Could not register foreground WinEvent hook");
    locationChangeHook_ = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr,
                                          &App::winEventProc, 0, 0,
                                          WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (!locationChangeHook_) logger_.warning(L"Could not register location-change WinEvent hook");
}

void App::unregisterSystemIntegrations() {
    if (controller_) {
        KillTimer(controller_, kDisplayTimer);
        KillTimer(controller_, kMajorMoveTimer);
        KillTimer(controller_, kExposureCheckpointTimer);
        KillTimer(controller_, kBoostTimer);
        KillTimer(controller_, kMicroTimer);
        UnregisterHotKey(controller_, kHotkeyId);
        UnregisterHotKey(controller_, kPositioningEscapeHotkeyId);
        WTSUnRegisterSessionNotification(controller_);
    }
    if (consoleDisplayPower_) UnregisterPowerSettingNotification(consoleDisplayPower_);
    if (monitorPower_) UnregisterPowerSettingNotification(monitorPower_);
    consoleDisplayPower_ = nullptr;
    monitorPower_ = nullptr;
    if (foregroundHook_) UnhookWinEvent(foregroundHook_);
    foregroundHook_ = nullptr;
    if (locationChangeHook_) UnhookWinEvent(locationChangeHook_);
    locationChangeHook_ = nullptr;
}

void App::createTrayIcon() {
    if (trayCreated_ || !controller_) return;
    trayIcon_ = NOTIFYICONDATAW{sizeof(NOTIFYICONDATAW)};
    trayIcon_.hWnd = controller_;
    trayIcon_.uID = 1;
    trayIcon_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    trayIcon_.uCallbackMessage = trayMessage_;
    trayIcon_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(trayIcon_.szTip, L"Adaptive OLED Clock");
    trayCreated_ = Shell_NotifyIconW(NIM_ADD, &trayIcon_) != FALSE;
    if (trayCreated_) {
        Shell_NotifyIconW(NIM_SETVERSION, &trayIcon_);
        trayFailureLogged_ = false;
    } else if (!trayFailureLogged_) {
        logger_.warning(L"Could not create the tray icon; the application will continue without it");
        trayFailureLogged_ = true;
    }
}

void App::removeTrayIcon() {
    if (trayCreated_) Shell_NotifyIconW(NIM_DELETE, &trayIcon_);
    trayCreated_ = false;
}

void App::showTrayMenu(POINT screenPoint) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, kTrayToggle, clockVisible_ ? L"Hide Clock" : L"Show Clock");
    AppendMenuW(menu, MF_STRING, kTrayBrightness, L"Temporary Brightness");
    HMENU movement = CreatePopupMenu();
    AppendMenuW(movement, MF_STRING | (settings_.movementMode == core::MovementMode::EdgeOnly ? MF_CHECKED : 0),
                kTrayModeEdge, L"Edge-only (recommended)");
    AppendMenuW(movement, MF_STRING | (settings_.movementMode == core::MovementMode::WholeScreen ? MF_CHECKED : 0),
                kTrayModeWhole, L"Whole screen");
    AppendMenuW(movement, MF_STRING | (settings_.movementMode == core::MovementMode::LocalWander ? MF_CHECKED : 0),
                kTrayModeLocal, L"Local wander");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(movement), L"Movement Mode");
    HMENU opacity = CreatePopupMenu();
    AppendMenuW(opacity, MF_STRING | (nearlyEqual(settings_.opacity, 0.16) ? MF_CHECKED : 0), kTrayOpacity16, L"16% normal opacity");
    AppendMenuW(opacity, MF_STRING | (nearlyEqual(settings_.opacity, 0.24) ? MF_CHECKED : 0), kTrayOpacity24, L"24% normal opacity");
    AppendMenuW(opacity, MF_STRING | (nearlyEqual(settings_.opacity, 0.32) ? MF_CHECKED : 0), kTrayOpacity32, L"32% normal opacity");
    AppendMenuW(opacity, MF_STRING | (nearlyEqual(settings_.opacity, 0.45) ? MF_CHECKED : 0), kTrayOpacity45, L"45% normal opacity");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(opacity), L"Clock Opacity");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTraySettings, L"Settings");
    AppendMenuW(menu, MF_STRING, kTrayStatistics, L"Exposure Statistics");
    AppendMenuW(menu, MF_STRING, kTrayPosition, positioning_ ? L"End positioning mode" : L"Drag to position");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayExit, L"Exit");
    SetForegroundWindow(controller_);
    const UINT command = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                        screenPoint.x, screenPoint.y, 0, controller_, nullptr);
    DestroyMenu(menu);
    if (command != 0) handleTrayCommand(command);
    PostMessageW(controller_, WM_NULL, 0, 0);
}

void App::handleTrayCommand(UINT command) {
    switch (command) {
    case kTrayToggle: toggleClock(); break;
    case kTrayBrightness: triggerBrightnessBoost(); break;
    case kTrayModeEdge: { auto next = settings_; next.movementMode = core::MovementMode::EdgeOnly; applySettings(next, true); break; }
    case kTrayModeWhole: { auto next = settings_; next.movementMode = core::MovementMode::WholeScreen; applySettings(next, true); break; }
    case kTrayModeLocal: { auto next = settings_; next.movementMode = core::MovementMode::LocalWander; applySettings(next, true); break; }
    case kTrayOpacity16: { auto next = settings_; next.opacity = 0.16; applySettings(next, true); break; }
    case kTrayOpacity24: { auto next = settings_; next.opacity = 0.24; applySettings(next, true); break; }
    case kTrayOpacity32: { auto next = settings_; next.opacity = 0.32; applySettings(next, true); break; }
    case kTrayOpacity45: { auto next = settings_; next.opacity = 0.45; applySettings(next, true); break; }
    case kTraySettings: settingsWindow_.show(settings_, monitors_); break;
    case kTrayStatistics: showStatisticsWindow(); break;
    case kTrayPosition: if (positioning_) endPositioningMode(); else beginPositioningMode(); break;
    case kTrayExit: PostQuitMessage(0); break;
    default: break;
    }
}

void App::showStatisticsWindow() {
    const std::string selected = selectedMonitor_ ? selectedMonitor_->stableKey : std::string{};
    statisticsWindow_.show(monitors_, exposure_, selected, settings_.movementMode);
}

void App::refreshOpenStatisticsWindow() {
    if (!statisticsWindow_.visible()) return;
    // An empty requested key tells the modeless window to preserve its valid
    // user selection; refresh() never recenters or activates the window.
    statisticsWindow_.refresh(monitors_, exposure_, {}, settings_.movementMode);
}

void App::resetExposureHistory() {
    if (MessageBoxW(statisticsWindow_.visible() ? statisticsWindow_.hwnd() : controller_,
                    L"Reset all saved exposure history for every monitor? This cannot be undone.",
                    L"Reset exposure history", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
        return;
    }
    // Settle against the old map first; otherwise updateExposureState would
    // charge the interval before the reset into the newly cleared history.
    exposureTracker_.settle(std::chrono::steady_clock::now());
    exposure_.clear();
    persistExposure();
    updateExposureState();
    if (statisticsWindow_.visible()) {
        const std::string selected = selectedMonitor_ ? selectedMonitor_->stableKey : std::string{};
        statisticsWindow_.refresh(monitors_, exposure_, selected, settings_.movementMode);
    }
    logger_.info(L"Exposure history reset by user");
}

void App::exportExposureCsv() {
    wchar_t path[MAX_PATH] = L"adaptive-oled-clock-exposure.csv";
    OPENFILENAMEW dialog{sizeof(OPENFILENAMEW)};
    dialog.hwndOwner = statisticsWindow_.visible() ? statisticsWindow_.hwnd() : controller_;
    dialog.lpstrFilter = L"CSV files (*.csv)\0*.csv\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path));
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    dialog.lpstrDefExt = L"csv";
    if (!GetSaveFileNameW(&dialog)) return;
    std::ofstream output(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!output) {
        MessageBoxW(dialog.hwndOwner, L"The CSV file could not be opened for writing.", L"Export failed", MB_OK | MB_ICONERROR);
        return;
    }
    output << "monitor_key,column,row,charged_seconds\n";
    output << std::setprecision(17);
    for (const auto& [key, map] : exposure_.maps()) {
        for (std::size_t row = 0; row < core::kExposureRows; ++row) {
            for (std::size_t column = 0; column < core::kExposureColumns; ++column) {
                output << csvQuote(key) << ',' << column + 1 << ',' << row + 1 << ',' << map.cell(column, row) << '\n';
            }
        }
    }
    output.flush();
    if (!output) {
        MessageBoxW(dialog.hwndOwner, L"The CSV export failed while writing the selected file.", L"Export failed", MB_OK | MB_ICONERROR);
        return;
    }
    logger_.info(L"Exposure statistics exported to CSV");
}

void App::refreshMonitorsAndPlacement(bool preservePosition, bool honorPreferredPosition) {
    monitors_ = monitorService_.enumerate();
    for (auto& monitor : monitors_) monitor.displayOn = displayOn_;
    const core::MonitorSelection selection = core::selectMonitor(monitors_, settings_);
    if (!selection.selected.has_value()) {
        selectedMonitor_.reset();
        currentClockRect_ = {};
        macroAnchorRect_ = {};
        overlay_.hide();
        updateExposureState();
        return;
    }
    const bool monitorChanged = !selectedMonitor_.has_value() ||
                                selectedMonitor_->stableKey != selection.selected->stableKey;
    if (monitorChanged) macroHistory_.clear();
    selectedMonitor_ = selection.selected;
    if (selection.usedPrimaryFallback) logger_.warning(L"Fixed monitor was missing; primary fallback selected");
    core::PlacementContext context = makePlacementContext();
    const bool currentStillValid = currentClockRect_.isValid() && !monitorChanged && preservePosition &&
                                   core::isValidPlacement(currentClockRect_, context);
    if (!currentStillValid) {
        context.previousRectPx.reset();
        std::optional<core::RectI> placement = honorPreferredPosition ? core::preferredPlacement(context) : std::nullopt;
        if (!placement.has_value()) {
            const core::ExposureMap* map = exposure_.find(selectedMonitor_->stableKey);
            const auto chosen = core::choosePlacement(context, map ? *map : core::ExposureMap{});
            if (chosen.has_value()) placement = chosen->boundsPx;
        }
        currentClockRect_ = placement.value_or(core::RectI{});
        macroAnchorRect_ = {};
        if (currentClockRect_.isValid()) {
            rememberMacroPosition(currentClockRect_);
            logger_.info(L"Initial/refresh placement selected");
        }
    }
    updateExposureState();
}

void App::refreshFullscreenState() {
    const bool next = core::isFullscreenLike(captureForegroundSnapshot());
    if (next != fullscreen_) {
        fullscreen_ = next;
        logger_.info(next ? L"Fullscreen policy entered" : L"Fullscreen policy cleared");
        updateExposureState();
        renderAndPresent();
    }
}

void App::refreshDisplayState(bool displayOn) {
    if (displayOn_ == displayOn) return;
    displayOn_ = displayOn;
    logger_.info(displayOn_ ? L"Display power state: on" : L"Display power state: off");
    refreshMonitorsAndPlacement(true);
    renderAndPresent();
}

void App::refreshTimeAndRender() {
    renderAndPresent();
    scheduleTimeBoundary();
    scheduleMicroBoundary();
}

void App::renderAndPresent() {
    if (!selectedMonitor_.has_value()) return;
    const std::uint32_t dpi = selectedMonitor_->dpiX == 0 ? 96 : selectedMonitor_->dpiX;
    core::SizeD measuredText{};
    core::SizeD measuredSurface{};
    if (!renderer_.renderText(currentTimeText(), fromUtf8(settings_.fontFamily), settings_.fontWeight,
                              settings_.fontSizeDip, settings_.textColor, currentOpacity(), dpi, positioning_,
                              measuredText, measuredSurface)) {
        logger_.error(L"Clock text rendering failed");
        return;
    }
    const bool sizeChanged = !nearlyEqual(measuredText.width, renderedSizeDip_.width) ||
                             !nearlyEqual(measuredText.height, renderedSizeDip_.height);
    renderedSizeDip_ = measuredText;
    renderedSurfaceDip_ = measuredSurface;
    core::PlacementContext context = makePlacementContext();
    const int expectedWidth = static_cast<int>(std::ceil(core::dipToPixels(measuredText.width, dpi)));
    const int expectedHeight = static_cast<int>(std::ceil(core::dipToPixels(measuredText.height, dpi)));
    if (sizeChanged || !currentClockRect_.isValid() || currentClockRect_.width() != expectedWidth ||
        currentClockRect_.height() != expectedHeight || !core::isValidPlacement(currentClockRect_, context)) {
        context.previousRectPx.reset();
        std::optional<core::RectI> placement = core::preferredPlacement(context);
        if (!placement.has_value()) {
            const core::ExposureMap* map = exposure_.find(selectedMonitor_->stableKey);
            const auto chosen = core::choosePlacement(context, map ? *map : core::ExposureMap{});
            if (chosen.has_value()) placement = chosen->boundsPx;
        }
        if (placement.has_value()) {
            currentClockRect_ = *placement;
            rememberMacroPosition(currentClockRect_);
        }
    }
    const bool permitted = displayOn_ && sessionUnlocked_ &&
                           (positioning_ || (clockVisible_ && (!settings_.hideInFullscreen || !fullscreen_))) &&
                           currentClockRect_.isValid();
    if (permitted) {
        const int padding = positioning_ ? static_cast<int>(std::lround(core::dipToPixels(kPositioningPaddingDip, dpi))) : 0;
        const int surfaceWidth = static_cast<int>(std::ceil(core::dipToPixels(renderedSurfaceDip_.width, dpi)));
        const int surfaceHeight = static_cast<int>(std::ceil(core::dipToPixels(renderedSurfaceDip_.height, dpi)));
        const core::RectI surface{currentClockRect_.left - padding, currentClockRect_.top - padding,
                                  currentClockRect_.left - padding + std::max(currentClockRect_.width() + padding * 2, surfaceWidth),
                                  currentClockRect_.top - padding + std::max(currentClockRect_.height() + padding * 2, surfaceHeight)};
        RECT windowRect{surface.left, surface.top, surface.right, surface.bottom};
        overlay_.moveResize(windowRect);
        if (!renderer_.present(overlay_.hwnd(), POINT{windowRect.left, windowRect.top})) {
            logger_.warning(L"Layered window presentation failed");
        }
        overlay_.show();
    } else {
        overlay_.hide();
    }
    updateExposureState();
}

void App::scheduleTimeBoundary() {
    if (!controller_) return;
    const auto now = std::chrono::system_clock::now();
    const auto boundary = settings_.showSeconds ? core::nextSecondBoundary(now) : core::nextMinuteBoundary(now);
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(boundary - now).count();
    milliseconds = std::clamp<long long>(milliseconds, 20, 0xFFFFFFFE);
    SetTimer(controller_, kDisplayTimer, static_cast<UINT>(milliseconds), nullptr);
}

void App::scheduleMicroBoundary() {
    if (!controller_) return;
    if (!settings_.showSeconds) {
        KillTimer(controller_, kMicroTimer);
        return;
    }
    const auto now = std::chrono::system_clock::now();
    const auto boundary = core::nextMinuteBoundary(now);
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(boundary - now).count();
    milliseconds = std::clamp<long long>(milliseconds, 50, 0xFFFFFFFE);
    SetTimer(controller_, kMicroTimer, static_cast<UINT>(milliseconds), nullptr);
}

void App::scheduleMajorMove() {
    if (!controller_) return;
    const UINT interval = static_cast<UINT>(std::clamp(settings_.movementIntervalMinutes, 1, 120) * 60'000);
    SetTimer(controller_, kMajorMoveTimer, interval, nullptr);
    nextMajorMove_ = std::chrono::steady_clock::now() + std::chrono::minutes(settings_.movementIntervalMinutes);
}

void App::applyMajorMove() {
    if (!selectedMonitor_.has_value() || !clockVisible_ || positioning_ || !displayOn_ || !sessionUnlocked_ ||
        (settings_.hideInFullscreen && fullscreen_)) {
        scheduleMajorMove();
        return;
    }
    core::PlacementContext context = makePlacementContext();
    context.previousRectPx = currentClockRect_;
    const core::ExposureMap* map = exposure_.find(selectedMonitor_->stableKey);
    const auto candidate = core::choosePlacement(context, map ? *map : core::ExposureMap{});
    if (candidate.has_value() && candidate->boundsPx != currentClockRect_) {
        currentClockRect_ = candidate->boundsPx;
        rememberMacroPosition(currentClockRect_);
        ++placementSeed_;
        logger_.info(L"Major exposure-balanced relocation");
        renderAndPresent();
    }
    scheduleMajorMove();
}

void App::applyMicroShift() {
    if (!selectedMonitor_.has_value() || !clockVisible_ || positioning_ || !displayOn_ || !sessionUnlocked_ ||
        (settings_.hideInFullscreen && fullscreen_) || !settings_.microShiftEnabled ||
        !currentClockRect_.isValid() || !macroAnchorRect_.isValid()) return;
    const std::uint32_t dpi = selectedMonitor_->dpiX == 0 ? 96 : selectedMonitor_->dpiX;
    const core::RectI anchor{macroAnchorRect_.left, macroAnchorRect_.top,
                             macroAnchorRect_.left + currentClockRect_.width(),
                             macroAnchorRect_.top + currentClockRect_.height()};
    const core::RectI shifted = core::applyBoundedMicroShift(currentClockRect_, anchor,
                                                              settings_.microShiftRadiusDip, dpi, placementSeed_++);
    const core::PlacementContext context = makePlacementContext();
    if (!core::isValidPlacement(shifted, context)) return;
    currentClockRect_ = shifted;
    logger_.info(L"Minute micro-shift applied within macro cage");
    renderAndPresent();
}

void App::rememberMacroPosition(const core::RectI& rect) {
    if (!rect.isValid()) return;
    if (macroAnchorRect_.isValid() && macroAnchorRect_ != rect) {
        macroHistory_.erase(std::remove(macroHistory_.begin(), macroHistory_.end(), macroAnchorRect_), macroHistory_.end());
        macroHistory_.insert(macroHistory_.begin(), macroAnchorRect_);
        if (macroHistory_.size() > 12) macroHistory_.resize(12);
    }
    macroAnchorRect_ = rect;
}

void App::setClockVisible(bool visible, const wchar_t* reason) {
    if (clockVisible_ == visible) return;
    clockVisible_ = visible;
    settings_.clockVisible = visible;
    logger_.info(std::wstring(visible ? L"Clock shown: " : L"Clock hidden: ") + reason);
    persistSettings();
    renderAndPresent();
}

void App::toggleClock() { setClockVisible(!clockVisible_, L"user toggle"); }

void App::triggerBrightnessBoost() {
    brightnessBoosted_ = true;
    KillTimer(controller_, kBoostTimer);
    logger_.info(L"Temporary brightness boost started");
    const UINT durationMilliseconds = static_cast<UINT>(std::clamp(settings_.boostDurationSeconds, 1, 300) * 1000);
    if (SetTimer(controller_, kBoostTimer, durationMilliseconds, nullptr) == 0) {
        logger_.warning(L"Could not schedule temporary brightness restoration");
    }
    renderAndPresent();
}

void App::beginPositioningMode() {
    if (!selectedMonitor_.has_value()) return;
    positioning_ = true;
    settingsWindow_.hide();
    overlay_.setInteractive(true);
    if (RegisterHotKey(controller_, kPositioningEscapeHotkeyId, 0, VK_ESCAPE) == FALSE) {
        logger_.warning(L"Could not register temporary positioning Escape hotkey; tray exit remains available");
    }
    logger_.info(L"Drag-to-position mode started");
    renderAndPresent();
}

void App::endPositioningMode() {
    if (!positioning_) return;
    positioning_ = false;
    UnregisterHotKey(controller_, kPositioningEscapeHotkeyId);
    overlay_.setInteractive(false);
    if (selectedMonitor_.has_value() && currentClockRect_.isValid()) {
        settings_.preferredPosition = core::physicalPointToNormalized(currentClockRect_.center(), selectedMonitor_->boundsPx);
        settings_.preferredPositionEnabled = true;
        rememberMacroPosition(currentClockRect_);
        persistSettings();
    }
    logger_.info(L"Drag-to-position mode ended");
    renderAndPresent();
}

void App::handleDraggedPoint(POINT screenPoint) {
    if (!positioning_ || !selectedMonitor_.has_value() || !currentClockRect_.isValid()) return;
    const std::uint32_t dpi = selectedMonitor_->dpiX == 0 ? 96 : selectedMonitor_->dpiX;
    const int padding = static_cast<int>(std::lround(core::dipToPixels(kPositioningPaddingDip, dpi)));
    core::PlacementContext context = makePlacementContext();
    const int width = currentClockRect_.width();
    const int height = currentClockRect_.height();
    core::RectI candidate{screenPoint.x + padding, screenPoint.y + padding,
                          screenPoint.x + padding + width, screenPoint.y + padding + height};
    if (!core::isValidPlacement(candidate, context)) {
        const auto validCandidates = core::generateCandidates(context);
        const auto nearest = std::min_element(validCandidates.begin(), validCandidates.end(), [&](const core::RectI& first,
                                                                                                   const core::RectI& second) {
            const double firstDistance = std::hypot(first.center().x - candidate.center().x,
                                                    first.center().y - candidate.center().y);
            const double secondDistance = std::hypot(second.center().x - candidate.center().x,
                                                     second.center().y - candidate.center().y);
            return firstDistance < secondDistance;
        });
        if (nearest != validCandidates.end()) candidate = *nearest;
    }
    if (core::isValidPlacement(candidate, context)) {
        currentClockRect_ = candidate;
        renderAndPresent();
    }
}

void App::handleDragFinished() { logger_.info(L"Drag-to-position pointer release"); }

bool App::persistSettings() {
    const bool saved = saveSettings(paths_, settings_);
    if (!saved) {
        logger_.warning(L"Settings persistence failed");
        return false;
    }
    // Direct durable mutations (visibility and drag-to-position) use this same
    // path, so the baseline advances only after the atomic save succeeds.
    committedSettings_ = settings_;
    return true;
}

void App::persistExposure() {
    if (!saveExposure(paths_, exposure_)) logger_.warning(L"Exposure persistence failed");
}

void App::applySettings(const core::Settings& incoming, bool committed) {
    const core::Settings previousLive = settings_;
    const core::Settings previousCommitted = committedSettings_;
    core::Settings next = incoming;
    next.validateAndNormalize();

    struct SettingsDiff {
        bool secondsChanged{false};
        bool appearanceChanged{false};
        bool movementModeChanged{false};
        bool placementPolicyChanged{false};
        bool monitorSelectionChanged{false};
        bool movementIntervalChanged{false};
        bool hotkeyChanged{false};
        bool startupChanged{false};
        bool anyChanged{false};
    };
    const auto diffSettings = [](const core::Settings& previous, const core::Settings& current) {
        SettingsDiff diff;
        diff.secondsChanged = previous.showSeconds != current.showSeconds;
        diff.appearanceChanged = previous.timeFormat != current.timeFormat ||
                                 previous.showAmPm != current.showAmPm ||
                                 previous.showSeconds != current.showSeconds ||
                                 previous.showDate != current.showDate ||
                                 previous.fontFamily != current.fontFamily ||
                                 previous.fontWeight != current.fontWeight ||
                                 previous.fontSizeDip != current.fontSizeDip ||
                                 previous.textColor != current.textColor ||
                                 previous.opacity != current.opacity ||
                                 previous.boostOpacity != current.boostOpacity;
        diff.movementModeChanged = previous.movementMode != current.movementMode;
        diff.placementPolicyChanged = diff.movementModeChanged ||
                                      previous.allowedArea != current.allowedArea ||
                                      previous.excludedAreas != current.excludedAreas ||
                                      previous.edgeMarginDip != current.edgeMarginDip ||
                                      previous.preferredPosition != current.preferredPosition ||
                                      previous.preferredPositionEnabled != current.preferredPositionEnabled;
        diff.monitorSelectionChanged = previous.monitorMode != current.monitorMode ||
                                       previous.fixedMonitorKey != current.fixedMonitorKey;
        diff.movementIntervalChanged = previous.movementIntervalMinutes != current.movementIntervalMinutes;
        diff.hotkeyChanged = previous.hotkeyEnabled != current.hotkeyEnabled;
        diff.startupChanged = previous.launchAtStartup != current.launchAtStartup;
        diff.anyChanged = previous.version != current.version || diff.appearanceChanged || diff.placementPolicyChanged ||
                          diff.monitorSelectionChanged || diff.movementIntervalChanged ||
                          previous.microShiftEnabled != current.microShiftEnabled ||
                          previous.microShiftRadiusDip != current.microShiftRadiusDip ||
                          previous.hideInFullscreen != current.hideInFullscreen ||
                          diff.startupChanged || diff.hotkeyChanged || previous.clockVisible != current.clockVisible ||
                          previous.boostDurationSeconds != current.boostDurationSeconds;
        return diff;
    };

    const SettingsDiff liveDiff = diffSettings(previousLive, next);
    const SettingsDiff committedDiff = diffSettings(previousCommitted, next);
    // The live state drives previews; the committed baseline drives persistence,
    // registry/hotkey integrations, placement refreshes, and deferred timers.
    const bool changed = liveDiff.anyChanged || (committed && committedDiff.anyChanged);
    if (!changed) return;

    const bool appearanceChanged = liveDiff.appearanceChanged || (committed && committedDiff.appearanceChanged);
    const bool placementPolicyChanged = liveDiff.placementPolicyChanged ||
                                        (committed && committedDiff.placementPolicyChanged);
    const bool monitorSelectionChanged = liveDiff.monitorSelectionChanged ||
                                         (committed && committedDiff.monitorSelectionChanged);
    const bool hideInFullscreenChanged = previousLive.hideInFullscreen != next.hideInFullscreen ||
                                         (committed && previousCommitted.hideInFullscreen != next.hideInFullscreen);
    const bool clockVisibilityChanged = previousLive.clockVisible != next.clockVisible ||
                                        (committed && previousCommitted.clockVisible != next.clockVisible);

    settings_ = next;
    clockVisible_ = settings_.clockVisible;
    const bool settingsSaved = !committed || !committedDiff.anyChanged || persistSettings();

    // Registry and hotkey state are external integrations; appearance previews
    // must not touch them, and committed no-op changes must not churn them.
    if (committed && committedDiff.startupChanged) setStartupRegistration();
    if (committed && committedDiff.hotkeyChanged && controller_) {
        UnregisterHotKey(controller_, kHotkeyId);
        if (settings_.hotkeyEnabled && RegisterHotKey(controller_, kHotkeyId,
                                                       MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'C') == FALSE) {
            logger_.warning(L"Could not apply Ctrl+Alt+C hotkey setting");
        }
    }

    if (committed && (committedDiff.monitorSelectionChanged || committedDiff.placementPolicyChanged)) {
        // A policy change is an anchor change, not merely a redraw: this forces
        // Edge-only/allowed-area semantics immediately while preferred placement
        // remains eligible only when it is valid under the new policy.
        refreshMonitorsAndPlacement(false, !committedDiff.movementModeChanged);
    }
    if (appearanceChanged || placementPolicyChanged || monitorSelectionChanged || hideInFullscreenChanged ||
        clockVisibilityChanged) {
        renderAndPresent();
    }
    if (committed && committedDiff.secondsChanged) {
        scheduleTimeBoundary();
        scheduleMicroBoundary();
    }
    if (committed && committedDiff.movementIntervalChanged) scheduleMajorMove();
    refreshOpenStatisticsWindow();
    if (committed && committedDiff.anyChanged) {
        logger_.info(settingsSaved ? L"Settings changed and persisted" : L"Settings changed; persistence failed");
    }
}

void App::setStartupRegistration() {
    if (!setLaunchAtStartup(settings_.launchAtStartup, modulePath())) {
        logger_.warning(L"Could not update HKCU Run startup registration");
    }
}

void App::updateExposureState() {
    if (!selectedMonitor_.has_value() || !currentClockRect_.isValid()) {
        exposureTracker_.setState({}, {}, false, std::chrono::steady_clock::now());
        return;
    }
    const bool permitted = displayOn_ && sessionUnlocked_ &&
                           (positioning_ || (clockVisible_ && (!settings_.hideInFullscreen || !fullscreen_)));
    const core::NormalizedRect normalized = core::physicalToNormalized(currentClockRect_, selectedMonitor_->boundsPx);
    exposureTracker_.setState(selectedMonitor_->stableKey, normalized, permitted, std::chrono::steady_clock::now());
}

void App::logState(const std::wstring& message) { logger_.info(message); }

core::MonitorInfo* App::selectedMonitor() {
    return selectedMonitor_.has_value() ? &selectedMonitor_.value() : nullptr;
}

const core::MonitorInfo* App::selectedMonitor() const {
    return selectedMonitor_.has_value() ? &selectedMonitor_.value() : nullptr;
}

RECT App::currentScreenRect() const {
    return {currentClockRect_.left, currentClockRect_.top, currentClockRect_.right, currentClockRect_.bottom};
}

double App::currentOpacity() const noexcept {
    return brightnessBoosted_ ? settings_.boostOpacity : settings_.opacity;
}

std::wstring App::currentTimeText() const {
    SYSTEMTIME systemTime{};
    GetLocalTime(&systemTime);
    std::tm localTime{};
    localTime.tm_year = systemTime.wYear - 1900;
    localTime.tm_mon = systemTime.wMonth - 1;
    localTime.tm_mday = systemTime.wDay;
    localTime.tm_hour = systemTime.wHour;
    localTime.tm_min = systemTime.wMinute;
    localTime.tm_sec = systemTime.wSecond;
    std::wstring result = core::formatClockText(localTime, settings_.timeFormat, settings_.showAmPm,
                                                localeHourMode_, settings_.showSeconds);
    if (settings_.showDate) {
        wchar_t date[128]{};
        if (GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &systemTime, nullptr,
                            date, static_cast<int>(std::size(date)), nullptr) > 0) {
            result += L"\n";
            result += date;
        }
    }
    return result;
}

core::PlacementContext App::makePlacementContext() const {
    core::PlacementContext context;
    if (!selectedMonitor_.has_value()) return context;
    context.monitorBoundsPx = selectedMonitor_->boundsPx;
    context.policyBoundsPx = settings_.movementMode == core::MovementMode::WholeScreen
                                 ? selectedMonitor_->boundsPx
                                 : selectedMonitor_->workAreaPx;
    context.dpi = selectedMonitor_->dpiX == 0 ? 96 : selectedMonitor_->dpiX;
    context.clockSizeDip = renderedSizeDip_;
    context.edgeMarginDip = settings_.edgeMarginDip;
    context.allowedArea = settings_.allowedArea;
    context.excludedAreas = settings_.excludedAreas;
    context.mode = settings_.movementMode;
    if (settings_.preferredPositionEnabled) context.preferredCenter = settings_.preferredPosition;
    context.previousRectPx = std::nullopt;
    context.recentMacroRects = macroHistory_;
    if (positioning_) {
        const double extraWidth = std::max(0.0, renderedSurfaceDip_.width - renderedSizeDip_.width) / 2.0;
        const double extraHeight = std::max(0.0, renderedSurfaceDip_.height - renderedSizeDip_.height) / 2.0;
        context.surfacePaddingDip = kPositioningPaddingDip + std::max(extraWidth, extraHeight);
    }
    context.randomSeed = placementSeed_;
    return context;
}

LRESULT CALLBACK App::controllerWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<App*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->controller_ = window;
    }
    return self ? self->handleControllerMessage(message, wParam, lParam)
                : DefWindowProcW(window, message, wParam, lParam);
}

void CALLBACK App::winEventProc(HWINEVENTHOOK, DWORD event, HWND window, LONG objectId, LONG childId, DWORD, DWORD) {
    if (!g_eventApp || !g_eventApp->controller_) return;
    if (event == EVENT_SYSTEM_FOREGROUND) {
        PostMessageW(g_eventApp->controller_, kWinEventMessage, event, reinterpret_cast<LPARAM>(window));
        return;
    }
    if (event == EVENT_OBJECT_LOCATIONCHANGE && window == GetForegroundWindow() &&
        objectId == OBJID_WINDOW && childId == CHILDID_SELF) {
        PostMessageW(g_eventApp->controller_, kWinEventMessage, event, reinterpret_cast<LPARAM>(window));
    }
}

LRESULT App::handleControllerMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (taskbarCreatedMessage_ && message == taskbarCreatedMessage_) {
        trayCreated_ = false;
        createTrayIcon();
        return 0;
    }
    if (message == kShowSettingsMessage) {
        settingsWindow_.show(settings_, monitors_);
        return 0;
    }
    if (message == trayMessage_) {
        const UINT event = static_cast<UINT>(lParam);
        if (event == WM_RBUTTONUP || event == WM_LBUTTONUP || event == WM_CONTEXTMENU) {
            POINT cursor{};
            GetCursorPos(&cursor);
            showTrayMenu(cursor);
        }
        return 0;
    }
    switch (message) {
    case WM_HOTKEY:
        if (wParam == kHotkeyId) toggleClock();
        else if (wParam == kPositioningEscapeHotkeyId && positioning_) endPositioningMode();
        return 0;
    case WM_TIMER:
        if (wParam == kDisplayTimer) {
            if (!settings_.showSeconds) applyMicroShift();
            refreshTimeAndRender();
        } else if (wParam == kMicroTimer) {
            applyMicroShift();
            scheduleMicroBoundary();
        } else if (wParam == kMajorMoveTimer) {
            applyMajorMove();
        } else if (wParam == kExposureCheckpointTimer) {
            exposureTracker_.checkpoint(std::chrono::steady_clock::now());
            persistExposure();
            refreshOpenStatisticsWindow();
        } else if (wParam == kBoostTimer) {
            KillTimer(controller_, kBoostTimer);
            brightnessBoosted_ = false;
            logger_.info(L"Temporary brightness boost restored normal opacity");
            renderAndPresent();
        }
        return 0;
    case WM_TIMECHANGE:
        logger_.info(L"System time/timezone change received");
        refreshTimeAndRender();
        refreshFullscreenState();
        return 0;
    case WM_DISPLAYCHANGE:
        logger_.info(L"Display topology change received");
        refreshMonitorsAndPlacement(true);
        renderAndPresent();
        return 0;
    case WM_POWERBROADCAST:
        if (wParam == PBT_POWERSETTINGCHANGE && lParam) {
            const auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(lParam);
            if (IsEqualGUID(setting->PowerSetting, GUID_CONSOLE_DISPLAY_STATE) ||
                IsEqualGUID(setting->PowerSetting, GUID_MONITOR_POWER_ON)) {
                const bool on = setting->DataLength >= sizeof(DWORD) && *reinterpret_cast<const DWORD*>(setting->Data) != 0;
                refreshDisplayState(on);
            }
        } else if (wParam == PBT_APMSUSPEND) {
            refreshDisplayState(false);
        } else if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) {
            logger_.info(L"Resume received; re-enumerating display and placement state");
            displayOn_ = true;
            refreshMonitorsAndPlacement(false);
            refreshFullscreenState();
            refreshTimeAndRender();
        }
        return TRUE;
    case WM_WTSSESSION_CHANGE:
        if (wParam == WTS_SESSION_LOCK) {
            sessionUnlocked_ = false;
            logger_.info(L"Session locked");
        } else if (wParam == WTS_SESSION_UNLOCK) {
            sessionUnlocked_ = true;
            logger_.info(L"Session unlocked");
            refreshMonitorsAndPlacement(true);
        }
        updateExposureState();
        renderAndPresent();
        return 0;
    case kWinEventMessage:
        refreshFullscreenState();
        return 0;
    case kDpiMessage:
        logger_.info(L"DPI change received; refreshing geometry");
        refreshMonitorsAndPlacement(true);
        renderAndPresent();
        return 0;
    case WM_COMMAND:
        handleTrayCommand(LOWORD(wParam));
        return 0;
    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(controller_, message, wParam, lParam);
}

} // namespace aoc::platform

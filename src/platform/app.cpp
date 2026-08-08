#include "aoc/platform/app.h"

#include "aoc/core/fullscreen.h"
#include "aoc/core/geometry.h"
#include "aoc/core/monitor.h"
#include "aoc/core/placement.h"
#include "aoc/platform/fullscreen_service.h"
#include "aoc/platform/startup.h"

#include <windows.h>
#include <commctrl.h>
#include <powerbase.h>
#include <shellapi.h>
#include <winnls.h>
#include <wtsapi32.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace aoc::platform {
namespace {

constexpr wchar_t kControllerClass[] = L"AdaptiveOledClockControllerWindow";
constexpr int kHotkeyId = 1;
constexpr int kPositioningEscapeHotkeyId = 2;
constexpr UINT_PTR kMinuteTimer = 1;
constexpr UINT_PTR kMajorMoveTimer = 2;
constexpr UINT_PTR kExposureCheckpointTimer = 3;
constexpr UINT_PTR kBoostTimer = 4;
constexpr UINT kWinEventMessage = WM_APP + 2;
constexpr UINT kDpiMessage = WM_APP + 3;

constexpr UINT kTrayToggle = 4000;
constexpr UINT kTrayBrightness = 4001;
constexpr UINT kTrayModeWhole = 4002;
constexpr UINT kTrayModeLocal = 4003;
constexpr UINT kTrayOpacity16 = 4004;
constexpr UINT kTrayOpacity24 = 4005;
constexpr UINT kTrayOpacity32 = 4006;
constexpr UINT kTrayOpacity45 = 4007;
constexpr UINT kTraySettings = 4008;
constexpr UINT kTrayStatistics = 4009;
constexpr UINT kTrayPosition = 4010;
constexpr UINT kTrayExit = 4011;

App* g_eventApp = nullptr;

std::wstring modulePath() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    return path;
}

bool nearlyEqual(double first, double second) {
    return std::abs(first - second) < 0.01;
}

} // namespace

App::App(HINSTANCE instance, int showCommand)
    : instance_(instance),
      showCommand_(showCommand),
      paths_(resolveDataPaths()),
      logger_(paths_.logFile),
      settings_(core::Settings::defaults()),
      exposureTracker_(exposure_) {}

App::~App() { shutdown(); }

int App::run() {
    if (!initialize()) {
        shutdown();
        return 1;
    }
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    shutdown();
    return static_cast<int>(message.wParam);
}

bool App::initialize() {
    if (initialized_) return true;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    comInitialized_ = SUCCEEDED(comResult);
    INITCOMMONCONTROLSEX commonControls{sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&commonControls);
    createControllerWindow();
    if (!controller_) return false;

    const LoadedData loaded = loadData(paths_);
    settings_ = loaded.settings;
    exposure_ = loaded.exposure;
    clockVisible_ = settings_.clockVisible;
    if (loaded.settingsRecovered) logger_.warning(L"Settings recovery/defaults were used");
    if (loaded.exposureRecovered) logger_.warning(L"Exposure recovery/defaults were used");

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
                                [this](const core::Settings& settings) { applySettings(settings); },
                                [this]() {
                                    applySettings(core::Settings::defaults());
                                    settingsWindow_.show(settings_, monitors_);
                                },
                                [this]() {
                                    applySettings(core::Settings::oledSafePreset());
                                    settingsWindow_.show(settings_, monitors_);
                                },
                                [this]() { beginPositioningMode(); })) {
        logger_.error(L"Settings window creation failed");
        return false;
    }

    core::SizeD initialSize{};
    if (!renderer_.renderText(currentTimeText(), settings_.fontSizeDip, settings_.textColor,
                              currentOpacity(), 96, initialSize)) {
        logger_.error(L"Initial clock text rendering failed");
        return false;
    }
    renderedSizeDip_ = initialSize.width > 0.0 ? initialSize : core::SizeD{80.0, 32.0};
    refreshMonitorsAndPlacement(false);
    initializeSystemIntegrations();
    createTrayIcon();
    setStartupRegistration();
    scheduleMinuteBoundary();
    scheduleMajorMove();
    SetTimer(controller_, kExposureCheckpointTimer, 60'000, nullptr);
    refreshFullscreenState();
    renderAndPresent();
    initialized_ = true;
    logger_.info(L"Adaptive OLED Clock started");
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
        settingsWindow_.hide();
        overlay_.hide();
        overlay_.destroy();
        if (controller_) DestroyWindow(controller_);
        controller_ = nullptr;
    }
    if (comInitialized_) {
        CoUninitialize();
        comInitialized_ = false;
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
    g_eventApp = this;
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
    KillTimer(controller_, kMinuteTimer);
    KillTimer(controller_, kMajorMoveTimer);
    KillTimer(controller_, kExposureCheckpointTimer);
    KillTimer(controller_, kBoostTimer);
    if (controller_) UnregisterHotKey(controller_, kHotkeyId);
    if (controller_) UnregisterHotKey(controller_, kPositioningEscapeHotkeyId);
    if (sessionNotificationId_ || controller_) WTSUnRegisterSessionNotification(controller_);
    sessionNotificationId_ = 0;
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
    if (trayCreated_) Shell_NotifyIconW(NIM_SETVERSION, &trayIcon_);
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
    AppendMenuW(movement, MF_STRING | (settings_.movementMode == core::MovementMode::WholeScreen ? MF_CHECKED : 0),
                kTrayModeWhole, L"Whole screen");
    AppendMenuW(movement, MF_STRING | (settings_.movementMode == core::MovementMode::LocalWander ? MF_CHECKED : 0),
                kTrayModeLocal, L"Local wander");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(movement), L"Movement Mode");
    HMENU opacity = CreatePopupMenu();
    AppendMenuW(opacity, MF_STRING, kTrayOpacity16, L"16% normal opacity");
    AppendMenuW(opacity, MF_STRING, kTrayOpacity24, L"24% normal opacity");
    AppendMenuW(opacity, MF_STRING | (nearlyEqual(settings_.opacity, 0.32) ? MF_CHECKED : 0), kTrayOpacity32, L"32% normal opacity");
    AppendMenuW(opacity, MF_STRING, kTrayOpacity45, L"45% normal opacity");
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
    case kTrayToggle:
        toggleClock();
        break;
    case kTrayBrightness:
        triggerBrightnessBoost();
        break;
    case kTrayModeWhole:
        settings_.movementMode = core::MovementMode::WholeScreen;
        applySettings(settings_);
        break;
    case kTrayModeLocal:
        settings_.movementMode = core::MovementMode::LocalWander;
        applySettings(settings_);
        break;
    case kTrayOpacity16:
        settings_.opacity = 0.16;
        applySettings(settings_);
        break;
    case kTrayOpacity24:
        settings_.opacity = 0.24;
        applySettings(settings_);
        break;
    case kTrayOpacity32:
        settings_.opacity = 0.32;
        applySettings(settings_);
        break;
    case kTrayOpacity45:
        settings_.opacity = 0.45;
        applySettings(settings_);
        break;
    case kTraySettings:
        settingsWindow_.show(settings_, monitors_);
        break;
    case kTrayStatistics: {
        const core::ExposureMap* map = selectedMonitor_ ? exposure_.find(selectedMonitor_->stableKey) : nullptr;
        const double total = map ? map->totalSeconds() : 0.0;
        std::wostringstream message;
        message << L"Current monitor exposure summary\n\nCharged time: " << std::fixed << std::setprecision(1)
                << total << L" seconds\nGrid: 12 columns x 8 rows\n\nThis Phase 1 summary intentionally contains no historical analytics.";
        MessageBoxW(controller_, message.str().c_str(), L"Exposure Statistics", MB_OK | MB_ICONINFORMATION);
        break;
    }
    case kTrayPosition:
        if (positioning_) endPositioningMode(); else beginPositioningMode();
        break;
    case kTrayExit:
        PostQuitMessage(0);
        break;
    default:
        break;
    }
}

void App::refreshMonitorsAndPlacement(bool preservePosition) {
    monitors_ = monitorService_.enumerate();
    for (auto& monitor : monitors_) monitor.displayOn = displayOn_;
    const core::MonitorSelection selection = core::selectMonitor(monitors_, settings_);
    if (!selection.selected.has_value()) {
        selectedMonitor_.reset();
        currentClockRect_ = {};
        overlay_.hide();
        updateExposureState();
        return;
    }
    const bool monitorChanged = !selectedMonitor_.has_value() ||
                                selectedMonitor_->stableKey != selection.selected->stableKey;
    selectedMonitor_ = selection.selected;
    if (selection.usedPrimaryFallback) logger_.warning(L"Fixed monitor was missing; primary fallback selected");
    core::PlacementContext context = makePlacementContext();
    const bool currentStillValid = currentClockRect_.isValid() && !monitorChanged && preservePosition &&
                                   core::isValidPlacement(currentClockRect_, context);
    if (!currentStillValid) {
        context.previousRectPx.reset();
        std::optional<core::RectI> placement = core::preferredPlacement(context);
        if (!placement.has_value()) {
            const core::ExposureMap* map = exposure_.find(selectedMonitor_->stableKey);
            const auto chosen = core::choosePlacement(context, map ? *map : core::ExposureMap{});
            if (chosen.has_value()) placement = chosen->boundsPx;
        }
        currentClockRect_ = placement.value_or(core::RectI{});
        if (currentClockRect_.isValid()) logger_.info(L"Initial/refresh placement selected");
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
    scheduleMinuteBoundary();
}

void App::renderAndPresent() {
    if (!selectedMonitor_.has_value()) return;
    const std::uint32_t dpi = selectedMonitor_->dpiX == 0 ? 96 : selectedMonitor_->dpiX;
    core::SizeD measured{};
    if (!renderer_.renderText(currentTimeText(), settings_.fontSizeDip, settings_.textColor,
                              currentOpacity(), dpi, measured)) {
        logger_.error(L"Clock text rendering failed");
        return;
    }
    const bool sizeChanged = !nearlyEqual(measured.width, renderedSizeDip_.width) ||
                             !nearlyEqual(measured.height, renderedSizeDip_.height);
    renderedSizeDip_ = measured;
    core::PlacementContext context = makePlacementContext();
    const int expectedWidth = static_cast<int>(std::ceil(core::dipToPixels(measured.width, dpi)));
    const int expectedHeight = static_cast<int>(std::ceil(core::dipToPixels(measured.height, dpi)));
    if (sizeChanged || !currentClockRect_.isValid() || currentClockRect_.width() != expectedWidth ||
        currentClockRect_.height() != expectedHeight || !core::isValidPlacement(currentClockRect_, context)) {
        context.previousRectPx.reset();
        if (currentClockRect_.isValid()) {
            context.preferredCenter = core::physicalPointToNormalized(currentClockRect_.center(), selectedMonitor_->boundsPx);
        }
        std::optional<core::RectI> placement = core::preferredPlacement(context);
        if (!placement.has_value()) {
            const core::ExposureMap* map = exposure_.find(selectedMonitor_->stableKey);
            const auto chosen = core::choosePlacement(context, map ? *map : core::ExposureMap{});
            if (chosen.has_value()) placement = chosen->boundsPx;
        }
        if (placement.has_value()) currentClockRect_ = *placement;
    }
    const bool permitted = displayOn_ && sessionUnlocked_ &&
                           (positioning_ || (clockVisible_ && (!settings_.hideInFullscreen || !fullscreen_))) &&
                           currentClockRect_.isValid();
    if (permitted) {
        RECT windowRect{currentClockRect_.left, currentClockRect_.top,
                        currentClockRect_.right, currentClockRect_.bottom};
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

void App::scheduleMinuteBoundary() {
    if (!controller_) return;
    const auto now = std::chrono::system_clock::now();
    const auto boundary = core::nextMinuteBoundary(now);
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(boundary - now).count();
    milliseconds = std::clamp<long long>(milliseconds, 50, 0xFFFFFFFE);
    SetTimer(controller_, kMinuteTimer, static_cast<UINT>(milliseconds), nullptr);
}

void App::scheduleMajorMove() {
    if (!controller_) return;
    const UINT interval = static_cast<UINT>(std::clamp(settings_.movementIntervalMinutes, 1, 120) * 60'000);
    SetTimer(controller_, kMajorMoveTimer, interval, nullptr);
    nextMajorMove_ = std::chrono::steady_clock::now() + std::chrono::minutes(settings_.movementIntervalMinutes);
}

void App::applyMajorMove() {
    if (!selectedMonitor_.has_value() || !clockVisible_ || positioning_) {
        scheduleMajorMove();
        return;
    }
    core::PlacementContext context = makePlacementContext();
    context.previousRectPx = currentClockRect_;
    const core::ExposureMap* map = exposure_.find(selectedMonitor_->stableKey);
    const auto candidate = core::choosePlacement(context, map ? *map : core::ExposureMap{});
    if (candidate.has_value() && candidate->boundsPx != currentClockRect_) {
        currentClockRect_ = candidate->boundsPx;
        ++placementSeed_;
        logger_.info(L"Major exposure-balanced relocation");
        renderAndPresent();
    }
    scheduleMajorMove();
}

void App::applyMicroShift() {
    if (!selectedMonitor_.has_value() || !clockVisible_ || positioning_ || !currentClockRect_.isValid()) return;
    std::mt19937_64 generator(placementSeed_++);
    std::uniform_real_distribution<double> shift(-2.0, 2.0);
    core::PlacementContext context = makePlacementContext();
    const int dx = static_cast<int>(std::lround(core::dipToPixels(shift(generator), context.dpi)));
    const int dy = static_cast<int>(std::lround(core::dipToPixels(shift(generator), context.dpi)));
    core::RectI shifted{currentClockRect_.left + dx, currentClockRect_.top + dy,
                        currentClockRect_.right + dx, currentClockRect_.bottom + dy};
    if (!core::isValidPlacement(shifted, context)) return;
    currentClockRect_ = shifted;
    logger_.info(L"Minute micro-shift applied");
    renderAndPresent();
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
        persistSettings();
    }
    logger_.info(L"Drag-to-position mode ended");
    renderAndPresent();
}

void App::handleDraggedPoint(POINT screenPoint) {
    if (!positioning_ || !selectedMonitor_.has_value() || !currentClockRect_.isValid()) return;
    core::PlacementContext context = makePlacementContext();
    const int width = currentClockRect_.width();
    const int height = currentClockRect_.height();
    core::RectI candidate{screenPoint.x, screenPoint.y, screenPoint.x + width, screenPoint.y + height};
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

void App::handleDragFinished() {
    logger_.info(L"Drag-to-position pointer release");
}

void App::persistSettings() {
    if (!saveSettings(paths_, settings_)) logger_.warning(L"Settings persistence failed");
}

void App::persistExposure() {
    if (!saveExposure(paths_, exposure_)) logger_.warning(L"Exposure persistence failed");
}

void App::applySettings(const core::Settings& incoming) {
    settings_ = incoming;
    settings_.validateAndNormalize();
    clockVisible_ = settings_.clockVisible;
    persistSettings();
    setStartupRegistration();
    if (controller_) {
        UnregisterHotKey(controller_, kHotkeyId);
        if (settings_.hotkeyEnabled && RegisterHotKey(controller_, kHotkeyId,
                                                       MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'C') == FALSE) {
            logger_.warning(L"Could not apply Ctrl+Alt+C hotkey setting");
        }
    }
    logger_.info(L"Settings changed and persisted");
    refreshMonitorsAndPlacement(true);
    renderAndPresent();
    scheduleMajorMove();
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
    if (!brightnessBoosted_) return settings_.opacity;
    return settings_.boostOpacity;
}

std::wstring App::currentTimeText() const {
    SYSTEMTIME systemTime{};
    GetLocalTime(&systemTime);
    std::tm localTime{};
    localTime.tm_hour = systemTime.wHour;
    localTime.tm_min = systemTime.wMinute;
    localTime.tm_sec = systemTime.wSecond;
    return core::formatClockText(localTime, settings_.timeFormat, settings_.showAmPm, localeHourMode_);
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
    context.preferredCenter = settings_.preferredPosition;
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
        if (wParam == kMinuteTimer) {
            applyMicroShift();
            refreshTimeAndRender();
        } else if (wParam == kMajorMoveTimer) {
            applyMajorMove();
        } else if (wParam == kExposureCheckpointTimer) {
            exposureTracker_.checkpoint(std::chrono::steady_clock::now());
            persistExposure();
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

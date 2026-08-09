#include "aoc/platform/settings_window.h"

#include "aoc/core/geometry.h"
#include "aoc/platform/win32_ui.h"

#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace aoc::platform {
namespace {

constexpr wchar_t kSettingsClass[] = L"AdaptiveOledClockSettingsWindow";
enum ControlId {
    IdTabs = 900,
    IdTimeFormat = 1000,
    IdShowAmPm,
    IdShowSeconds,
    IdShowDate,
    IdFontFamily,
    IdFontWeight,
    IdFontSize,
    IdColor,
    IdOpacity,
    IdBoostOpacity,
    IdBoostDuration,
    IdMovementMode,
    IdInterval,
    IdMicroShiftEnabled,
    IdMicroShiftRadius,
    IdEdgeMargin,
    IdAllowedPreset,
    IdAllowedLeft,
    IdAllowedTop,
    IdAllowedRight,
    IdAllowedBottom,
    IdPreferredEnabled,
    IdExcludedLeft,
    IdExcludedTop,
    IdExcludedRight,
    IdExcludedBottom,
    IdExcludedList,
    IdExcludedAdd,
    IdExcludedRemove,
    IdExcludedClear,
    IdMonitorMode,
    IdFullscreen,
    IdStartup,
    IdHotkey,
    IdReset,
    IdPreset,
    IdPosition,
    IdStatistics,
    IdClose,
};

constexpr UINT_PTR kPreviewApplyTimer = 7100;
constexpr UINT kPreviewApplyDelayMs = 90;
constexpr UINT kFinishShowSyncMessage = WM_APP + 0x3A;

void setText(HWND control, const std::wstring& value) {
    if (control) SetWindowTextW(control, value.c_str());
}

std::wstring numberText(double value) {
    std::wostringstream stream;
    stream << std::fixed << std::setprecision(1) << value;
    return stream.str();
}

double readDouble(HWND control, double fallback) {
    wchar_t buffer[128]{};
    if (!control || GetWindowTextW(control, buffer, static_cast<int>(std::size(buffer))) == 0) return fallback;
    wchar_t* end = nullptr;
    const double value = wcstod(buffer, &end);
    return end != buffer && std::isfinite(value) ? value : fallback;
}

bool nearlyEqual(double first, double second) { return std::abs(first - second) < 0.005; }

} // namespace

SettingsWindow::~SettingsWindow() {
    if (hwnd_) DestroyWindow(hwnd_);
}

bool SettingsWindow::create(HINSTANCE instance, HWND owner, ApplyCallback onApply,
                            SimpleCallback onReset, SimpleCallback onPreset,
                            SimpleCallback onPositioning, SimpleCallback onStatistics) {
    if (hwnd_) return true;
    instance_ = instance;
    owner_ = owner;
    onApply_ = std::move(onApply);
    onReset_ = std::move(onReset);
    onPreset_ = std::move(onPreset);
    onPositioning_ = std::move(onPositioning);
    onStatistics_ = std::move(onStatistics);
    WNDCLASSEXW windowClass{sizeof(WNDCLASSEXW)};
    windowClass.hInstance = instance_;
    windowClass.lpfnWndProc = &SettingsWindow::windowProc;
    windowClass.lpszClassName = kSettingsClass;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    // The controller is HWND_MESSAGE-only, so it cannot own a normal desktop window
    // reliably. Keep it as the event sink, but create Settings as an independent
    // top-level window so it can be activated, enumerated, and used at every DPI.
    hwnd_ = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, kSettingsClass, L"Adaptive OLED Clock Settings",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_VSCROLL,
                            CW_USEDEFAULT, CW_USEDEFAULT, 860, 700, nullptr, nullptr, instance_, this);
    return hwnd_ != nullptr;
}

int SettingsWindow::scale(int value) const noexcept {
    return scaleDip(value, dpi_);
}

void SettingsWindow::setScrollOffset(int offset) {
    const int next = std::clamp(offset, 0, scrollMaximum_);
    if (next == verticalOffset_) return;
    verticalOffset_ = next;
    RECT client{};
    if (hwnd_ && GetClientRect(hwnd_, &client)) layoutControls(client.right, client.bottom);
}

void SettingsWindow::scrollBy(int amount) { setScrollOffset(verticalOffset_ + amount); }

void SettingsWindow::schedulePreviewApply() {
    if (!hwnd_) return;
    // Thumb-tracking is preview-only and coalesced so a fast drag cannot write
    // settings or repeat monitor/placement work for every pixel crossed.
    SetTimer(hwnd_, kPreviewApplyTimer, kPreviewApplyDelayMs, nullptr);
}

LRESULT CALLBACK SettingsWindow::pageControlSubclassProc(HWND control, UINT message, WPARAM wParam,
                                                         LPARAM lParam, UINT_PTR subclassId,
                                                         DWORD_PTR refData) {
    (void)subclassId;
    auto* self = reinterpret_cast<SettingsWindow*>(refData);
    if (self) {
        if (control == self->pageHost_ && (message == WM_COMMAND || message == WM_HSCROLL)) {
            return SendMessageW(self->hwnd_, message, wParam, lParam);
        }
        if (message == WM_MOUSEWHEEL) {
            wchar_t className[32]{};
            GetClassNameW(control, className, static_cast<int>(std::size(className)));
            if (wcscmp(className, L"ComboBox") == 0 || wcscmp(className, L"ListBox") == 0 ||
                wcscmp(className, TRACKBAR_CLASSW) == 0) {
                return DefSubclassProc(control, message, wParam, lParam);
            }
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            self->scrollBy(delta > 0 ? -self->scale(48) : self->scale(48));
            return 0;
        }
        if (message == WM_KEYDOWN && (wParam == VK_PRIOR || wParam == VK_NEXT)) {
            self->scrollBy(wParam == VK_PRIOR ? -self->scale(240) : self->scale(240));
            return 0;
        }
    }
    return DefSubclassProc(control, message, wParam, lParam);
}

void SettingsWindow::addPageControl(int page, HWND control) {
    if (control && page >= 0 && page < static_cast<int>(pages_.size())) {
        pages_[static_cast<std::size_t>(page)].controls.push_back(control);
        SetWindowSubclass(control, &SettingsWindow::pageControlSubclassProc, 1,
                          reinterpret_cast<DWORD_PTR>(this));
    }
    if (control) {
        allControls_.push_back(control);
        setControlFont(control, controlFont_.get());
    } else {
        controlCreationFailed_ = true;
    }
}

HWND SettingsWindow::addControl(int page, DWORD style, const wchar_t* className,
                                const wchar_t* text, int id, int x, int y,
                                int width, int height, DWORD exStyle) {
    const bool combo = wcscmp(className, L"COMBOBOX") == 0;
    const int creationHeight = combo ? scale(240) : std::max(scale(24), height);
    if (page >= 0 && !pageHost_) {
        controlCreationFailed_ = true;
        return nullptr;
    }
    const HWND parent = page >= 0 ? pageHost_ : hwnd_;
    HWND control = CreateWindowExW(exStyle, className, text, WS_CHILD | WS_VISIBLE | style,
                                   scale(x), scale(y), scale(width), creationHeight, parent,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    if (!control) {
        const std::wstring message = L"Adaptive OLED Clock: failed to create Settings control " +
                                     std::to_wstring(id) + L" (error " +
                                     std::to_wstring(GetLastError()) + L")\n";
        OutputDebugStringW(message.c_str());
    }
    addPageControl(page, control);
    if (combo && control) {
        // CBS_DROPDOWNLIST uses the creation height for its list; keep a real list extent even when collapsed.
        SendMessageW(control, CB_SETMINVISIBLE, 8, 0);
        SendMessageW(control, CB_SETDROPPEDWIDTH, scale(420), 0);
    }
    return control;
}

HWND SettingsWindow::addLabel(int page, const wchar_t* text) {
    return addControl(page, SS_LEFT | SS_NOPREFIX, L"STATIC", text, 0, 0, 0, 0, scale(24));
}

void SettingsWindow::show(const core::Settings& settings, const std::vector<core::MonitorInfo>& monitors) {
    if (!hwnd_) return;
    settings_ = settings;
    monitors_ = monitors;
    verticalOffset_ = 0;
    // Keep activation/layout notifications from reading the creation defaults
    // back into the loaded model before the final post-show synchronization.
    syncing_ = true;
    const RECT bounds = centeredWindowRect(owner_, hwnd_, 760, 600, dpi_);
    SetWindowPos(hwnd_, HWND_TOP, bounds.left, bounds.top,
                 bounds.right - bounds.left, bounds.bottom - bounds.top, SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd_);
    // Positioning/showing can deliver the first WM_SIZE/WM_DPICHANGED layout
    // transaction. Synchronize once afterward so loaded values are the final
    // writer, while syncToControls() keeps all notifications suppressed.
    syncToControls();
    // Focus notifications queued by activation can arrive after this call
    // returns; release suppression only after those queued messages drain.
    syncing_ = true;
    if (!PostMessageW(hwnd_, kFinishShowSyncMessage, 0, 0)) syncing_ = false;
}

void SettingsWindow::hide() {
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

bool SettingsWindow::createControls() {
    dpi_ = GetDpiForWindow(hwnd_);
    if (dpi_ == 0) dpi_ = 96;
    controlFont_.reset(createControlFont(dpi_));
    if (!controlFont_) controlCreationFailed_ = true;
    tabs_ = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP,
                            scale(12), scale(12), scale(836), scale(600), hwnd_,
                            reinterpret_cast<HMENU>(IdTabs), instance_, nullptr);
    if (tabs_) {
        allControls_.push_back(tabs_);
        setControlFont(tabs_, controlFont_.get());
        for (const wchar_t* title : {L"Clock", L"Movement && OLED", L"Display && Windows", L"Statistics"}) {
            TCITEMW item{TCIF_TEXT};
            item.pszText = const_cast<wchar_t*>(title);
            (void)TabCtrl_InsertItem(tabs_, TabCtrl_GetItemCount(tabs_), &item);
        }
    } else controlCreationFailed_ = true;
    // Page controls live under a clipped host so vertical scrolling never lets
    // a high-DPI page paint over the tab header or the fixed action row.
    pageHost_ = CreateWindowExW(WS_EX_CONTROLPARENT, L"STATIC", L"",
                                WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                0, 0, 1, 1, hwnd_, nullptr, instance_, nullptr);
    if (pageHost_) SetWindowSubclass(pageHost_, &SettingsWindow::pageControlSubclassProc, 1,
                                     reinterpret_cast<DWORD_PTR>(this));
    else controlCreationFailed_ = true;

    (void)createClockPage();
    (void)createMovementPage();
    (void)createDisplayPage();
    (void)createStatisticsPage();

    autoSaveLabel_ = addControl(-1, SS_LEFT | SS_NOPREFIX, L"STATIC",
                                L"Changes are saved automatically.", 0);
    resetButton_ = addControl(-1, BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", L"Reset defaults", IdReset);
    presetButton_ = addControl(-1, BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", L"OLED-safe preset", IdPreset);
    positioningButton_ = addControl(-1, BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", L"Drag to position", IdPosition);
    statisticsButton_ = addControl(-1, BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", L"Open statistics", IdStatistics);
    closeButton_ = addControl(-1, BS_DEFPUSHBUTTON | WS_TABSTOP, L"BUTTON", L"Close", IdClose);

    const auto setRange = [](HWND control, int minimum, int maximum, int tick = 1) {
        if (!control) return;
        SendMessageW(control, TBM_SETRANGE, TRUE, MAKELONG(minimum, maximum));
        SendMessageW(control, TBM_SETTICFREQ, tick, 0);
    };
    setRange(fontSize_, 8, 96, 8);
    setRange(opacity_, 0, 100, 10);
    setRange(boostOpacity_, 40, 100, 10);
    setRange(interval_, 1, 120, 10);
    setRange(microShiftRadius_, 0, 32, 4);
    setRange(edgeMargin_, 0, 96, 16);
    setActiveTab(0);
    return !controlCreationFailed_;
}

bool SettingsWindow::createClockPage() {
    timeFormatLabel_ = addLabel(0, L"Time format");
    timeFormat_ = addControl(0, CBS_DROPDOWNLIST | WS_VSCROLL, L"COMBOBOX", L"", IdTimeFormat);
    for (const wchar_t* value : {L"Locale-derived", L"12-hour", L"24-hour"})
        SendMessageW(timeFormat_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    showSeconds_ = addControl(0, BS_AUTOCHECKBOX | WS_TABSTOP, L"BUTTON", L"Show seconds (updates on real second boundaries)", IdShowSeconds);
    showDate_ = addControl(0, BS_AUTOCHECKBOX | WS_TABSTOP, L"BUTTON", L"Show a locale-formatted date", IdShowDate);
    showAmPm_ = addControl(0, BS_AUTOCHECKBOX | WS_TABSTOP, L"BUTTON", L"Show AM/PM when using 12-hour time", IdShowAmPm);
    fontFamilyLabel_ = addLabel(0, L"Font family");
    fontFamily_ = addControl(0, CBS_DROPDOWNLIST | WS_VSCROLL, L"COMBOBOX", L"", IdFontFamily);
    for (const wchar_t* value : {L"Segoe UI", L"Segoe UI Variable", L"Arial", L"Calibri", L"Consolas", L"Bahnschrift"})
        SendMessageW(fontFamily_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    fontWeightLabel_ = addLabel(0, L"Font weight");
    fontWeight_ = addControl(0, CBS_DROPDOWNLIST | WS_VSCROLL, L"COMBOBOX", L"", IdFontWeight);
    for (const wchar_t* value : {L"Normal", L"Semibold"})
        SendMessageW(fontWeight_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    fontSizeLabel_ = addLabel(0, L"Font size");
    fontSize_ = addControl(0, TBS_AUTOTICKS | TBS_HORZ | WS_TABSTOP, TRACKBAR_CLASSW, L"", IdFontSize);
    fontSizeValue_ = addControl(0, SS_LEFT, L"STATIC", L"", 0);
    opacityLabel_ = addLabel(0, L"Normal opacity");
    opacity_ = addControl(0, TBS_AUTOTICKS | TBS_HORZ | WS_TABSTOP, TRACKBAR_CLASSW, L"", IdOpacity);
    opacityValue_ = addControl(0, SS_LEFT, L"STATIC", L"", 0);
    colorLabel_ = addLabel(0, L"Text color");
    colorButton_ = addControl(0, BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", L"Choose color...", IdColor);
    boostOpacityLabel_ = addLabel(0, L"Temporary brightness");
    boostOpacity_ = addControl(0, TBS_AUTOTICKS | TBS_HORZ | WS_TABSTOP, TRACKBAR_CLASSW, L"", IdBoostOpacity);
    boostOpacityValue_ = addControl(0, SS_LEFT, L"STATIC", L"", 0);
    boostDurationLabel_ = addLabel(0, L"Boost duration");
    boostDuration_ = addControl(0, CBS_DROPDOWNLIST | WS_VSCROLL, L"COMBOBOX", L"", IdBoostDuration);
    for (const wchar_t* value : {L"5 seconds", L"10 seconds", L"15 seconds", L"30 seconds", L"60 seconds", L"120 seconds"})
        SendMessageW(boostDuration_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    return !controlCreationFailed_;
}

bool SettingsWindow::createMovementPage() {
    movementModeLabel_ = addLabel(1, L"Movement mode");
    movementMode_ = addControl(1, CBS_DROPDOWNLIST | WS_VSCROLL, L"COMBOBOX", L"", IdMovementMode);
    for (const wchar_t* value : {L"Edge-only (recommended)", L"Whole screen", L"Local wander"})
        SendMessageW(movementMode_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    intervalLabel_ = addLabel(1, L"Major movement interval");
    interval_ = addControl(1, TBS_AUTOTICKS | TBS_HORZ | WS_TABSTOP, TRACKBAR_CLASSW, L"", IdInterval);
    microShiftEnabled_ = addControl(1, BS_AUTOCHECKBOX | WS_TABSTOP, L"BUTTON", L"Enable minute micro-shifts", IdMicroShiftEnabled);
    microShiftRadiusLabel_ = addLabel(1, L"Micro-shift radius");
    microShiftRadius_ = addControl(1, TBS_AUTOTICKS | TBS_HORZ | WS_TABSTOP, TRACKBAR_CLASSW, L"", IdMicroShiftRadius);
    microShiftRadiusValue_ = addControl(1, SS_LEFT, L"STATIC", L"", 0);
    edgeMarginLabel_ = addLabel(1, L"Edge margin");
    edgeMargin_ = addControl(1, TBS_AUTOTICKS | TBS_HORZ | WS_TABSTOP, TRACKBAR_CLASSW, L"", IdEdgeMargin);
    edgeMarginValue_ = addControl(1, SS_LEFT, L"STATIC", L"", 0);
    allowedPresetLabel_ = addLabel(1, L"Allowed movement area");
    allowedPreset_ = addControl(1, CBS_DROPDOWNLIST | WS_VSCROLL, L"COMBOBOX", L"", IdAllowedPreset);
    for (const wchar_t* value : {L"Entire usable area", L"Center 80%", L"Center 60%", L"Custom"})
        SendMessageW(allowedPreset_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    allowedAreaHelp_ = addControl(1, SS_LEFT | SS_NOPREFIX, L"STATIC",
                                  L"Custom area (%) — enter Left, Top, Right, Bottom; values apply on focus loss.", 0);
    allowedLeftLabel_ = addControl(1, SS_CENTER, L"STATIC", L"Left", 0);
    allowedTopLabel_ = addControl(1, SS_CENTER, L"STATIC", L"Top", 0);
    allowedRightLabel_ = addControl(1, SS_CENTER, L"STATIC", L"Right", 0);
    allowedBottomLabel_ = addControl(1, SS_CENTER, L"STATIC", L"Bottom", 0);
    allowedLeft_ = addControl(1, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, L"EDIT", L"0", IdAllowedLeft);
    allowedTop_ = addControl(1, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, L"EDIT", L"0", IdAllowedTop);
    allowedRight_ = addControl(1, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, L"EDIT", L"100", IdAllowedRight);
    allowedBottom_ = addControl(1, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, L"EDIT", L"100", IdAllowedBottom);
    preferredEnabled_ = addControl(1, BS_AUTOCHECKBOX | WS_TABSTOP, L"BUTTON", L"Use the preferred position on startup", IdPreferredEnabled);
    preferredSummary_ = addControl(1, SS_LEFT, L"STATIC", L"", 0);
    excludedAreaLabel_ = addLabel(1, L"Excluded rectangle (percent of monitor)");
    excludedLeft_ = addControl(1, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, L"EDIT", L"0", IdExcludedLeft);
    excludedTop_ = addControl(1, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, L"EDIT", L"0", IdExcludedTop);
    excludedRight_ = addControl(1, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, L"EDIT", L"0", IdExcludedRight);
    excludedBottom_ = addControl(1, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, L"EDIT", L"0", IdExcludedBottom);
    excludedList_ = addControl(1, LBS_NOTIFY | WS_BORDER | WS_VSCROLL | WS_TABSTOP, L"LISTBOX", L"", IdExcludedList);
    excludedAdd_ = addControl(1, BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", L"Add exclusion", IdExcludedAdd);
    excludedRemove_ = addControl(1, BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", L"Remove selected", IdExcludedRemove);
    excludedClear_ = addControl(1, BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", L"Clear all", IdExcludedClear);
    return !controlCreationFailed_;
}

bool SettingsWindow::createDisplayPage() {
    monitorModeLabel_ = addLabel(2, L"Monitor");
    monitorMode_ = addControl(2, CBS_DROPDOWNLIST | WS_VSCROLL, L"COMBOBOX", L"", IdMonitorMode);
    fullscreen_ = addControl(2, BS_AUTOCHECKBOX | WS_TABSTOP, L"BUTTON", L"Hide while a fullscreen app is active", IdFullscreen);
    startup_ = addControl(2, BS_AUTOCHECKBOX | WS_TABSTOP, L"BUTTON", L"Launch at Windows sign-in", IdStartup);
    hotkey_ = addControl(2, BS_AUTOCHECKBOX | WS_TABSTOP, L"BUTTON", L"Enable global Ctrl+Alt+C visibility toggle", IdHotkey);
    displayHelp_ = addControl(2, SS_LEFT | SS_NOPREFIX, L"STATIC",
                              L"The overlay follows the primary monitor by default. A fixed choice is kept by Windows display identity and falls back safely if disconnected.",
                              0);
    return !controlCreationFailed_;
}

bool SettingsWindow::createStatisticsPage() {
    statisticsExplanation_ = addControl(3, SS_LEFT | SS_NOPREFIX, L"STATIC",
                                        L"Exposure is charged only while the rendered clock is visible and the display/session policy allows it. The statistics window shows the selected monitor's 12 x 8 heatmap.",
                                        0);
    statisticsHelp_ = addControl(3, SS_LEFT | SS_NOPREFIX, L"STATIC",
                                 L"Use the tray or the button below to inspect least/most exposed cells, charged time, and imbalance.",
                                 0);
    return !controlCreationFailed_;
}

void SettingsWindow::layoutControls(int width, int height) {
    if (!tabs_ || !pageHost_) return;
    width = std::max(1, width);
    height = std::max(1, height);
    const int margin = std::min(scale(16), std::max(0, (width - 1) / 2));
    const int actionGap = scale(8);
    const int actionButtonHeight = scale(30);
    const int actionNoteHeight = scale(20);
    const int actionNoteGap = scale(4);
    const int actionAvailableWidth = std::max(1, width - 2 * margin);
    const int actionNaturalWidths[] = {116, 132, 124, 130, 90};
    int actionRows = 1;
    int actionCursor = 0;
    for (const int naturalWidth : actionNaturalWidths) {
        const int buttonWidth = std::min(scale(naturalWidth), actionAvailableWidth);
        if (actionCursor > 0 && actionCursor + buttonWidth > actionAvailableWidth) {
            ++actionRows;
            actionCursor = 0;
        }
        actionCursor += buttonWidth + actionGap;
    }
    const int actionAreaTop = height - margin - actionNoteHeight - actionNoteGap -
                              actionRows * actionButtonHeight - (actionRows - 1) * actionGap;
    const int tabWidth = std::max(1, width - 2 * margin);
    const int tabHeight = std::max(1, actionAreaTop - actionGap - margin);
    MoveWindow(tabs_, margin, margin, tabWidth, tabHeight, TRUE);
    RECT page{};
    GetClientRect(tabs_, &page);
    TabCtrl_AdjustRect(tabs_, FALSE, &page);
    const int pageWidth = std::max(1, static_cast<int>(page.right - page.left));
    const int pageHeight = std::max(1, static_cast<int>(page.bottom - page.top));
    // The page host is a sibling of the tab control. Keep it explicitly above
    // the tab's page body; otherwise the tab can remain first in child Z-order
    // and paint over every page control even though they are visible and laid
    // out correctly.
    SetWindowPos(pageHost_, HWND_TOP, margin + page.left, margin + page.top,
                 pageWidth, pageHeight, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    const int left = 0;
    const bool compact = pageWidth < scale(640);
    const int fieldX = compact ? 0 : scale(300);
    const int fieldWidth = compact ? pageWidth : std::max(scale(260), pageWidth - scale(330));
    const int wideSliderWidth = compact ? pageWidth :
                                std::max(scale(120), pageWidth - fieldX - scale(130));
    const int wideValueX = fieldX + wideSliderWidth + scale(10);
    const int rowHeight = scale(30);
    int contentHeight = 0;

    for (auto& pageState : pages_) {
        for (HWND control : pageState.controls) ShowWindow(control, SW_HIDE);
    }
    auto show = [&](HWND control, int x, int y, int w, int h, int pageIndex) {
        if (!control || (pageIndex >= 0 && pageIndex != activeTab_)) return;
        const int adjustedY = pageIndex >= 0 ? y - verticalOffset_ : y;
        const bool inPageViewport = pageIndex < 0 || (adjustedY + h > 0 && adjustedY < pageHeight);
        MoveWindow(control, x, adjustedY, w, h, TRUE);
        ShowWindow(control, inPageViewport ? SW_SHOW : SW_HIDE);
    };
    auto label = [&](HWND control, int y, int pageIndex) {
        show(control, left, y, scale(270), rowHeight, pageIndex);
    };
    auto field = [&](HWND control, int y, int w = -1, int h = -1, int pageIndex = -1) {
        const int page = pageIndex < 0 ? activeTab_ : pageIndex;
        show(control, fieldX, y, w < 0 ? fieldWidth : scale(w), h < 0 ? rowHeight : scale(h), page);
    };
    auto labelAt = [&](HWND control, int x, int y, int w, int h) {
        show(control, x, y, w, h, activeTab_);
    };
    auto compactLabelField = [&](HWND labelControl, HWND fieldControl, int& y, int fieldHeightDip = 30) {
        show(labelControl, 0, y, pageWidth, rowHeight, activeTab_);
        y += rowHeight + scale(4);
        show(fieldControl, 0, y, pageWidth, scale(fieldHeightDip), activeTab_);
        y += scale(fieldHeightDip + 10);
    };
    auto compactCheck = [&](HWND control, int& y) {
        show(control, 0, y, pageWidth, scale(28), activeTab_);
        y += scale(38);
    };
    auto compactSlider = [&](HWND labelControl, HWND slider, HWND valueControl, int& y) {
        show(labelControl, 0, y, pageWidth, rowHeight, activeTab_);
        y += rowHeight + scale(4);
        show(slider, 0, y, pageWidth, scale(24), activeTab_);
        y += scale(28);
        if (valueControl) {
            show(valueControl, 0, y, pageWidth, rowHeight, activeTab_);
            y += scale(38);
        } else {
            y += scale(10);
        }
    };

    // Page children are positioned in physical pixels inside pageHost_; its clip
    // region and vertical offset keep high-DPI content above the fixed actions.
    if (activeTab_ == 0) {
        int y = scale(18);
        if (!compact) {
            label(timeFormatLabel_, y, 0); field(timeFormat_, y, -1, 30, 0); y += scale(38);
            field(showSeconds_, y, -1, 28, 0); y += scale(32);
            field(showDate_, y, -1, 28, 0); y += scale(32);
            field(showAmPm_, y, -1, 28, 0); y += scale(38);
            label(fontFamilyLabel_, y, 0); field(fontFamily_, y, -1, 30, 0); y += scale(38);
            label(fontWeightLabel_, y, 0); field(fontWeight_, y, -1, 30, 0); y += scale(38);
            label(fontSizeLabel_, y, 0); show(fontSize_, fieldX, y + scale(3), wideSliderWidth, scale(24), 0); labelAt(fontSizeValue_, wideValueX, y, scale(120), rowHeight); y += scale(38);
            label(opacityLabel_, y, 0); show(opacity_, fieldX, y + scale(3), wideSliderWidth, scale(24), 0); labelAt(opacityValue_, wideValueX, y, scale(120), rowHeight); y += scale(38);
            label(colorLabel_, y, 0); field(colorButton_, y, -1, 30, 0); y += scale(38);
            label(boostOpacityLabel_, y, 0); show(boostOpacity_, fieldX, y + scale(3), wideSliderWidth, scale(24), 0); labelAt(boostOpacityValue_, wideValueX, y, scale(120), rowHeight); y += scale(38);
            label(boostDurationLabel_, y, 0); field(boostDuration_, y, 220, 30, 0);
            contentHeight = y + rowHeight;
        } else {
            compactLabelField(timeFormatLabel_, timeFormat_, y);
            compactCheck(showSeconds_, y);
            compactCheck(showDate_, y);
            compactCheck(showAmPm_, y);
            compactLabelField(fontFamilyLabel_, fontFamily_, y);
            compactLabelField(fontWeightLabel_, fontWeight_, y);
            compactSlider(fontSizeLabel_, fontSize_, fontSizeValue_, y);
            compactSlider(opacityLabel_, opacity_, opacityValue_, y);
            compactLabelField(colorLabel_, colorButton_, y);
            compactSlider(boostOpacityLabel_, boostOpacity_, boostOpacityValue_, y);
            compactLabelField(boostDurationLabel_, boostDuration_, y);
            contentHeight = y;
        }
    } else if (activeTab_ == 1) {
        int y = scale(18);
        if (!compact) {
            label(movementModeLabel_, y, 1); field(movementMode_, y, -1, 30, 1); y += scale(42);
            label(intervalLabel_, y, 1); show(interval_, fieldX, y + scale(3), wideSliderWidth, scale(24), 1); y += scale(40);
            field(microShiftEnabled_, y, -1, 28, 1); y += scale(34);
            label(microShiftRadiusLabel_, y, 1); show(microShiftRadius_, fieldX, y + scale(3), wideSliderWidth, scale(24), 1); labelAt(microShiftRadiusValue_, wideValueX, y, scale(120), rowHeight); y += scale(40);
            label(edgeMarginLabel_, y, 1); show(edgeMargin_, fieldX, y + scale(3), wideSliderWidth, scale(24), 1); labelAt(edgeMarginValue_, wideValueX, y, scale(120), rowHeight); y += scale(40);
            label(allowedPresetLabel_, y, 1); field(allowedPreset_, y, -1, 30, 1); y += scale(40);
            labelAt(allowedAreaHelp_, left, y, pageWidth, rowHeight); y += scale(30);
            const int boxWidth = scale(64);
            const int boxGap = scale(8);
            for (const auto& [index, control] : std::array<std::pair<int, HWND>, 4>{
                     std::pair{0, allowedLeftLabel_}, std::pair{1, allowedTopLabel_},
                     std::pair{2, allowedRightLabel_}, std::pair{3, allowedBottomLabel_}}) {
                show(control, fieldX + index * (boxWidth + boxGap), y, boxWidth, scale(18), 1);
            }
            y += scale(18);
            for (const auto& [index, control] : std::array<std::pair<int, HWND>, 4>{
                     std::pair{0, allowedLeft_}, std::pair{1, allowedTop_},
                     std::pair{2, allowedRight_}, std::pair{3, allowedBottom_}}) {
                show(control, fieldX + index * (boxWidth + boxGap), y, boxWidth, rowHeight, 1);
            }
            y += scale(38);
            field(preferredEnabled_, y, -1, 28, 1); y += scale(32);
            labelAt(preferredSummary_, left, y, pageWidth, rowHeight); y += scale(38);
            labelAt(excludedAreaLabel_, left, y, scale(270), rowHeight); y += scale(30);
            show(excludedLeft_, fieldX, y, boxWidth, rowHeight, 1);
            show(excludedTop_, fieldX + boxWidth + boxGap, y, boxWidth, rowHeight, 1);
            show(excludedRight_, fieldX + 2 * (boxWidth + boxGap), y, boxWidth, rowHeight, 1);
            show(excludedBottom_, fieldX + 3 * (boxWidth + boxGap), y, boxWidth, rowHeight, 1);
            y += scale(38);
            show(excludedList_, fieldX, y, fieldWidth, scale(100), 1); y += scale(108);
            const int exclusionAvailableWidth = std::max(1, pageWidth - fieldX);
            int exclusionButtonX = fieldX;
            int exclusionButtonY = y;
            auto exclusionButton = [&](HWND button, int naturalWidth) {
                const int buttonWidth = std::min(scale(naturalWidth), exclusionAvailableWidth);
                if (exclusionButtonX != fieldX && exclusionButtonX + buttonWidth > pageWidth) {
                    exclusionButtonX = fieldX;
                    exclusionButtonY += rowHeight + scale(8);
                }
                show(button, exclusionButtonX, exclusionButtonY, buttonWidth, rowHeight, 1);
                exclusionButtonX += buttonWidth + scale(8);
            };
            exclusionButton(excludedAdd_, 110);
            exclusionButton(excludedRemove_, 132);
            exclusionButton(excludedClear_, 90);
            contentHeight = exclusionButtonY + rowHeight;
        } else {
            compactLabelField(movementModeLabel_, movementMode_, y);
            compactSlider(intervalLabel_, interval_, nullptr, y);
            compactCheck(microShiftEnabled_, y);
            compactSlider(microShiftRadiusLabel_, microShiftRadius_, microShiftRadiusValue_, y);
            compactSlider(edgeMarginLabel_, edgeMargin_, edgeMarginValue_, y);
            compactLabelField(allowedPresetLabel_, allowedPreset_, y);
            show(allowedAreaHelp_, 0, y, pageWidth, rowHeight, 1); y += scale(34);
            compactLabelField(allowedLeftLabel_, allowedLeft_, y);
            compactLabelField(allowedTopLabel_, allowedTop_, y);
            compactLabelField(allowedRightLabel_, allowedRight_, y);
            compactLabelField(allowedBottomLabel_, allowedBottom_, y);
            compactCheck(preferredEnabled_, y);
            show(preferredSummary_, 0, y, pageWidth, rowHeight, 1); y += scale(38);
            show(excludedAreaLabel_, 0, y, pageWidth, rowHeight, 1); y += scale(34);

            const int editGap = scale(8);
            const bool twoColumnEdits = pageWidth >= scale(220);
            const int editWidth = twoColumnEdits ? (pageWidth - editGap) / 2 : pageWidth;
            show(excludedLeft_, 0, y, editWidth, rowHeight, 1);
            show(excludedTop_, twoColumnEdits ? editWidth + editGap : 0, y, editWidth, rowHeight, 1);
            y += rowHeight + scale(8);
            show(excludedRight_, 0, y, editWidth, rowHeight, 1);
            show(excludedBottom_, twoColumnEdits ? editWidth + editGap : 0, y, editWidth, rowHeight, 1);
            y += rowHeight + scale(10);
            show(excludedList_, 0, y, pageWidth, scale(100), 1); y += scale(108);
            for (HWND button : {excludedAdd_, excludedRemove_, excludedClear_}) {
                show(button, 0, y, pageWidth, rowHeight, 1);
                y += rowHeight + scale(8);
            }
            contentHeight = y;
        }
    } else if (activeTab_ == 2) {
        int y = scale(18);
        if (!compact) {
            label(monitorModeLabel_, y, 2); field(monitorMode_, y, -1, 30, 2); y += scale(48);
            field(fullscreen_, y, -1, 28, 2); y += scale(36);
            field(startup_, y, -1, 28, 2); y += scale(36);
            field(hotkey_, y, -1, 28, 2); y += scale(52);
            labelAt(displayHelp_, left, y, pageWidth, scale(60));
            contentHeight = y + scale(60);
        } else {
            compactLabelField(monitorModeLabel_, monitorMode_, y);
            compactCheck(fullscreen_, y);
            compactCheck(startup_, y);
            compactCheck(hotkey_, y);
            show(displayHelp_, 0, y, pageWidth, scale(90), 2);
            contentHeight = y + scale(90);
        }
    } else {
        labelAt(statisticsExplanation_, left, scale(24), pageWidth, scale(60));
        labelAt(statisticsHelp_, left, scale(106), pageWidth, scale(60));
        contentHeight = scale(166);
    }

    const int nextMaximum = std::max(0, contentHeight - pageHeight);
    const int nextOffset = std::clamp(verticalOffset_, 0, nextMaximum);
    scrollMaximum_ = nextMaximum;
    ShowScrollBar(hwnd_, SB_VERT, scrollMaximum_ > 0);
    SCROLLINFO scrollInfo{sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE | SIF_POS};
    scrollInfo.nMin = 0;
    scrollInfo.nMax = std::max(0, contentHeight - 1);
    scrollInfo.nPage = static_cast<UINT>(pageHeight);
    scrollInfo.nPos = nextOffset;
    SetScrollInfo(hwnd_, SB_VERT, &scrollInfo, TRUE);
    if (nextOffset != verticalOffset_) {
        verticalOffset_ = nextOffset;
        layoutControls(width, height);
        return;
    }

    int actionX = margin;
    show(autoSaveLabel_, margin, actionAreaTop, actionAvailableWidth, actionNoteHeight, -1);
    int actionY = actionAreaTop + actionNoteHeight + actionNoteGap;
    auto actionButton = [&](HWND control, int naturalWidth) {
        const int buttonWidth = std::min(scale(naturalWidth), actionAvailableWidth);
        if (actionX != margin && actionX + buttonWidth > width - margin) {
            actionX = margin;
            actionY += actionButtonHeight + actionGap;
        }
        show(control, actionX, actionY, buttonWidth, actionButtonHeight, -1);
        actionX += buttonWidth + actionGap;
    };
    actionButton(resetButton_, 116);
    actionButton(presetButton_, 132);
    actionButton(positioningButton_, 124);
    actionButton(statisticsButton_, 130);
    actionButton(closeButton_, 90);
}

void SettingsWindow::setActiveTab(int tab) {
    activeTab_ = std::clamp(tab, 0, 3);
    verticalOffset_ = 0;
    if (tabs_) TabCtrl_SetCurSel(tabs_, activeTab_);
    RECT client{};
    GetClientRect(hwnd_, &client);
    layoutControls(client.right, client.bottom);
}

void SettingsWindow::syncClockPage() {
    SendMessageW(timeFormat_, CB_SETCURSEL, static_cast<int>(settings_.timeFormat), 0);
    SendMessageW(fontWeight_, CB_SETCURSEL, static_cast<int>(settings_.fontWeight), 0);
    SendMessageW(showAmPm_, BM_SETCHECK, settings_.showAmPm ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(showSeconds_, BM_SETCHECK, settings_.showSeconds ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(showDate_, BM_SETCHECK, settings_.showDate ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(fontSize_, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::lround(settings_.fontSizeDip)));
    SendMessageW(opacity_, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::lround(settings_.opacity * 100.0)));
    SendMessageW(boostOpacity_, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::lround(settings_.boostOpacity * 100.0)));
    int durationSelection = 0;
    for (const int duration : {5, 10, 15, 30, 60, 120}) {
        if (duration == settings_.boostDurationSeconds) break;
        ++durationSelection;
    }
    durationSelection = std::clamp(durationSelection, 0, 5);
    SendMessageW(boostDuration_, CB_SETCURSEL, durationSelection, 0);

    SendMessageW(fontFamily_, CB_RESETCONTENT, 0, 0);
    const std::wstring selectedFamily = wideFromUtf8(settings_.fontFamily);
    int familySelection = -1;
    for (const wchar_t* value : {L"Segoe UI", L"Segoe UI Variable", L"Arial", L"Calibri", L"Consolas", L"Bahnschrift"}) {
        const LRESULT index = SendMessageW(fontFamily_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
        if (selectedFamily == value) familySelection = static_cast<int>(index);
    }
    if (familySelection < 0 && !selectedFamily.empty()) {
        familySelection = static_cast<int>(SendMessageW(fontFamily_, CB_ADDSTRING, 0,
                                                         reinterpret_cast<LPARAM>(selectedFamily.c_str())));
    }
    SendMessageW(fontFamily_, CB_SETCURSEL, std::max(0, familySelection), 0);
    setText(colorButton_, L"Text color: " + std::to_wstring(settings_.textColor.r) + L", " +
                           std::to_wstring(settings_.textColor.g) + L", " + std::to_wstring(settings_.textColor.b));
}

void SettingsWindow::syncMovementPage() {
    SendMessageW(movementMode_, CB_SETCURSEL,
                 settings_.movementMode == core::MovementMode::EdgeOnly ? 0 :
                 settings_.movementMode == core::MovementMode::WholeScreen ? 1 : 2, 0);
    SendMessageW(microShiftEnabled_, BM_SETCHECK, settings_.microShiftEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(preferredEnabled_, BM_SETCHECK, settings_.preferredPositionEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(interval_, TBM_SETPOS, TRUE, settings_.movementIntervalMinutes);
    SendMessageW(microShiftRadius_, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::lround(settings_.microShiftRadiusDip)));
    SendMessageW(edgeMargin_, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::lround(settings_.edgeMarginDip)));
    SendMessageW(allowedPreset_, CB_SETCURSEL,
                 nearlyEqual(settings_.allowedArea.left, 0.0) && nearlyEqual(settings_.allowedArea.top, 0.0) &&
                 nearlyEqual(settings_.allowedArea.right, 1.0) && nearlyEqual(settings_.allowedArea.bottom, 1.0) ? 0 :
                 nearlyEqual(settings_.allowedArea.left, 0.1) && nearlyEqual(settings_.allowedArea.top, 0.1) &&
                 nearlyEqual(settings_.allowedArea.right, 0.9) && nearlyEqual(settings_.allowedArea.bottom, 0.9) ? 1 :
                 nearlyEqual(settings_.allowedArea.left, 0.2) && nearlyEqual(settings_.allowedArea.top, 0.2) &&
                 nearlyEqual(settings_.allowedArea.right, 0.8) && nearlyEqual(settings_.allowedArea.bottom, 0.8) ? 2 : 3, 0);
    setText(allowedLeft_, numberText(settings_.allowedArea.left * 100.0));
    setText(allowedTop_, numberText(settings_.allowedArea.top * 100.0));
    setText(allowedRight_, numberText(settings_.allowedArea.right * 100.0));
    setText(allowedBottom_, numberText(settings_.allowedArea.bottom * 100.0));
    updateAllowedAreaEditorState();
    setText(excludedLeft_, numberText(settings_.excludedAreas.empty() ? 0.0 : settings_.excludedAreas.back().left * 100.0));
    setText(excludedTop_, numberText(settings_.excludedAreas.empty() ? 0.0 : settings_.excludedAreas.back().top * 100.0));
    setText(excludedRight_, numberText(settings_.excludedAreas.empty() ? 0.0 : settings_.excludedAreas.back().right * 100.0));
    setText(excludedBottom_, numberText(settings_.excludedAreas.empty() ? 0.0 : settings_.excludedAreas.back().bottom * 100.0));
    updateExcludedList();
    setText(preferredSummary_, settings_.preferredPositionEnabled ? L"Enabled — the saved position will be used when valid."
                                                                  : L"Disabled — exposure-balanced placement will choose the initial anchor.");
}

void SettingsWindow::syncDisplayPage() {
    SendMessageW(fullscreen_, BM_SETCHECK, settings_.hideInFullscreen ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(startup_, BM_SETCHECK, settings_.launchAtStartup ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(hotkey_, BM_SETCHECK, settings_.hotkeyEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(monitorMode_, CB_RESETCONTENT, 0, 0);
    SendMessageW(monitorMode_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Follow primary monitor"));
    int monitorSelection = 0;
    for (std::size_t index = 0; index < monitors_.size(); ++index) {
        const auto& monitor = monitors_[index];
        std::wstring label = monitor.displayName.empty() ? wideFromUtf8(monitor.stableKey) : monitor.displayName;
        if (monitor.primary) label += L" (Primary)";
        SendMessageW(monitorMode_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        if (settings_.monitorMode == core::MonitorMode::Fixed && monitor.stableKey == settings_.fixedMonitorKey) {
            monitorSelection = static_cast<int>(index + 1);
        }
    }
    SendMessageW(monitorMode_, CB_SETCURSEL, monitorSelection, 0);
}

void SettingsWindow::syncToControls() {
    if (!hwnd_) return;
    syncing_ = true;
    syncClockPage();
    syncMovementPage();
    syncDisplayPage();
    updateSliderLabels();
    syncing_ = false;
}

void SettingsWindow::updateSliderLabels() {
    if (!fontSize_) return;
    const int fontSize = static_cast<int>(SendMessageW(fontSize_, TBM_GETPOS, 0, 0));
    const int opacity = static_cast<int>(SendMessageW(opacity_, TBM_GETPOS, 0, 0));
    const int boostOpacity = static_cast<int>(SendMessageW(boostOpacity_, TBM_GETPOS, 0, 0));
    const int radius = static_cast<int>(SendMessageW(microShiftRadius_, TBM_GETPOS, 0, 0));
    const int margin = static_cast<int>(SendMessageW(edgeMargin_, TBM_GETPOS, 0, 0));
    setText(opacityValue_, std::to_wstring(opacity) + L"% normal");
    setText(fontSizeValue_, std::to_wstring(fontSize) + L" DIP");
    setText(boostOpacityValue_, std::to_wstring(boostOpacity) + L"% for boost");
    setText(microShiftRadiusValue_, std::to_wstring(radius) + L" DIP radius");
    setText(edgeMarginValue_, std::to_wstring(margin) + L" DIP from edge");
    (void)fontSize;
}

void SettingsWindow::updateAllowedAreaEditorState() {
    const bool custom = allowedPreset_ && SendMessageW(allowedPreset_, CB_GETCURSEL, 0, 0) == 3;
    for (HWND control : {allowedLeft_, allowedTop_, allowedRight_, allowedBottom_}) {
        if (control) EnableWindow(control, custom ? TRUE : FALSE);
    }
}

void SettingsWindow::readClockPage(core::Settings& next) const {
    const int format = static_cast<int>(SendMessageW(timeFormat_, CB_GETCURSEL, 0, 0));
    next.timeFormat = format < 0 ? core::TimeFormat::Locale : static_cast<core::TimeFormat>(format);
    next.fontWeight = static_cast<core::FontWeight>(
        std::max(0, static_cast<int>(SendMessageW(fontWeight_, CB_GETCURSEL, 0, 0))));
    wchar_t family[256]{};
    const int familyLength = GetWindowTextW(fontFamily_, family, static_cast<int>(std::size(family)));
    if (familyLength > 0) next.fontFamily = utf8FromWide(std::wstring(family, family + familyLength));
    next.showAmPm = SendMessageW(showAmPm_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    next.showSeconds = SendMessageW(showSeconds_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    next.showDate = SendMessageW(showDate_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    next.fontSizeDip = static_cast<double>(SendMessageW(fontSize_, TBM_GETPOS, 0, 0));
    next.opacity = static_cast<double>(SendMessageW(opacity_, TBM_GETPOS, 0, 0)) / 100.0;
    next.boostOpacity = static_cast<double>(SendMessageW(boostOpacity_, TBM_GETPOS, 0, 0)) / 100.0;
    const int durationSelection = static_cast<int>(SendMessageW(boostDuration_, CB_GETCURSEL, 0, 0));
    constexpr int durations[] = {5, 10, 15, 30, 60, 120};
    if (durationSelection >= 0 && durationSelection < static_cast<int>(std::size(durations))) {
        next.boostDurationSeconds = durations[durationSelection];
    }
}

void SettingsWindow::readMovementPage(core::Settings& next) const {
    const int movement = static_cast<int>(SendMessageW(movementMode_, CB_GETCURSEL, 0, 0));
    next.movementMode = movement == 0 ? core::MovementMode::EdgeOnly :
                        movement == 1 ? core::MovementMode::WholeScreen : core::MovementMode::LocalWander;
    next.microShiftEnabled = SendMessageW(microShiftEnabled_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    next.preferredPositionEnabled = SendMessageW(preferredEnabled_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    next.movementIntervalMinutes = static_cast<int>(SendMessageW(interval_, TBM_GETPOS, 0, 0));
    next.microShiftRadiusDip = static_cast<double>(SendMessageW(microShiftRadius_, TBM_GETPOS, 0, 0));
    next.edgeMarginDip = static_cast<double>(SendMessageW(edgeMargin_, TBM_GETPOS, 0, 0));
    const int allowed = static_cast<int>(SendMessageW(allowedPreset_, CB_GETCURSEL, 0, 0));
    if (allowed == 0) next.allowedArea = {0.0, 0.0, 1.0, 1.0};
    else if (allowed == 1) next.allowedArea = {0.1, 0.1, 0.9, 0.9};
    else if (allowed == 2) next.allowedArea = {0.2, 0.2, 0.8, 0.8};
    else if (allowed == 3) {
        next.allowedArea = {
            readDouble(allowedLeft_, next.allowedArea.left * 100.0) / 100.0,
            readDouble(allowedTop_, next.allowedArea.top * 100.0) / 100.0,
            readDouble(allowedRight_, next.allowedArea.right * 100.0) / 100.0,
            readDouble(allowedBottom_, next.allowedArea.bottom * 100.0) / 100.0,
        };
    }
}

void SettingsWindow::readDisplayPage(core::Settings& next) const {
    next.hideInFullscreen = SendMessageW(fullscreen_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    next.launchAtStartup = SendMessageW(startup_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    next.hotkeyEnabled = SendMessageW(hotkey_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const int monitorSelection = static_cast<int>(SendMessageW(monitorMode_, CB_GETCURSEL, 0, 0));
    next.monitorMode = monitorSelection > 0 ? core::MonitorMode::Fixed : core::MonitorMode::FollowPrimary;
    next.fixedMonitorKey.clear();
    if (monitorSelection > 0 && static_cast<std::size_t>(monitorSelection - 1) < monitors_.size()) {
        next.fixedMonitorKey = monitors_[static_cast<std::size_t>(monitorSelection - 1)].stableKey;
    }
}

void SettingsWindow::applyFromControls(bool committed) {
    if (syncing_ || !timeFormat_) return;
    core::Settings next = settings_;
    readClockPage(next);
    readMovementPage(next);
    readDisplayPage(next);
    next.validateAndNormalize();
    settings_ = next;
    setText(allowedLeft_, numberText(settings_.allowedArea.left * 100.0));
    setText(allowedTop_, numberText(settings_.allowedArea.top * 100.0));
    setText(allowedRight_, numberText(settings_.allowedArea.right * 100.0));
    setText(allowedBottom_, numberText(settings_.allowedArea.bottom * 100.0));
    updateAllowedAreaEditorState();
    updateSliderLabels();
    setText(preferredSummary_, settings_.preferredPositionEnabled ? L"Enabled — the saved position will be used when valid."
                                                                  : L"Disabled — exposure-balanced placement will choose the initial anchor.");
    if (onApply_) onApply_(settings_, committed);
}

void SettingsWindow::addExcludedArea() {
    const core::NormalizedRect rect{readDouble(excludedLeft_, 0.0) / 100.0,
                                    readDouble(excludedTop_, 0.0) / 100.0,
                                    readDouble(excludedRight_, 0.0) / 100.0,
                                    readDouble(excludedBottom_, 0.0) / 100.0};
    if (!rect.isValid()) return;
    settings_.excludedAreas.push_back(core::clampNormalizedRect(rect));
    settings_.validateAndNormalize();
    updateExcludedList();
    if (onApply_) onApply_(settings_, true);
}

void SettingsWindow::removeSelectedExcludedArea() {
    const LRESULT selected = SendMessageW(excludedList_, LB_GETCURSEL, 0, 0);
    if (selected >= 0 && static_cast<std::size_t>(selected) < settings_.excludedAreas.size()) {
        settings_.excludedAreas.erase(settings_.excludedAreas.begin() + selected);
        updateExcludedList();
        if (onApply_) onApply_(settings_, true);
    }
}

void SettingsWindow::clearExcludedAreas() {
    settings_.excludedAreas.clear();
    updateExcludedList();
    if (onApply_) onApply_(settings_, true);
}

void SettingsWindow::updateExcludedList() {
    if (!excludedList_) return;
    SendMessageW(excludedList_, LB_RESETCONTENT, 0, 0);
    for (const auto& rect : settings_.excludedAreas) {
        std::wostringstream item;
        item << std::fixed << std::setprecision(1) << rect.left * 100.0 << L"%, " << rect.top * 100.0 << L"% - "
             << rect.right * 100.0 << L"%, " << rect.bottom * 100.0 << L"%";
        SendMessageW(excludedList_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.str().c_str()));
    }
}

void SettingsWindow::chooseTextColor() {
    CHOOSECOLORW chooser{sizeof(CHOOSECOLORW)};
    COLORREF customColors[16]{};
    chooser.hwndOwner = hwnd_;
    chooser.rgbResult = RGB(settings_.textColor.r, settings_.textColor.g, settings_.textColor.b);
    chooser.lpCustColors = customColors;
    chooser.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorW(&chooser)) {
        settings_.textColor = {GetRValue(chooser.rgbResult), GetGValue(chooser.rgbResult), GetBValue(chooser.rgbResult), 255};
        setText(colorButton_, L"Text color: " + std::to_wstring(settings_.textColor.r) + L", " +
                               std::to_wstring(settings_.textColor.g) + L", " + std::to_wstring(settings_.textColor.b));
        if (onApply_) onApply_(settings_, true);
    }
}

LRESULT CALLBACK SettingsWindow::windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<SettingsWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = window;
    }
    return self ? self->handleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT SettingsWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == kFinishShowSyncMessage) {
        syncing_ = false;
        return 0;
    }
    switch (message) {
    case WM_CREATE:
        return createControls() ? 0 : -1;
    case WM_SIZE:
        layoutControls(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        if (limits) {
            MONITORINFO info{sizeof(MONITORINFO)};
            const HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
            if (monitor && GetMonitorInfoW(monitor, &info)) {
                limits->ptMaxTrackSize.x = info.rcWork.right - info.rcWork.left;
                limits->ptMaxTrackSize.y = info.rcWork.bottom - info.rcWork.top;
                limits->ptMinTrackSize.x = std::min<LONG>(scale(520), limits->ptMaxTrackSize.x);
                limits->ptMinTrackSize.y = std::min<LONG>(scale(420), limits->ptMaxTrackSize.y);
            }
        }
        return 0;
    }
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wParam);
        verticalOffset_ = 0;
        UniqueGdiFont nextFont(createControlFont(dpi_));
        if (nextFont) {
            for (HWND control : allControls_) setControlFont(control, nextFont.get());
            // Controls now reference the replacement; moving it into the owner
            // releases the previous font only after the handoff is complete.
            controlFont_ = std::move(nextFont);
        }
        const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
        if (suggested) {
            const RECT fitted = aoc::platform::fitToWorkArea(*suggested);
            SetWindowPos(hwnd_, nullptr, fitted.left, fitted.top,
                         fitted.right - fitted.left, fitted.bottom - fitted.top,
                                    SWP_NOZORDER | SWP_NOACTIVATE);
        }
        RECT client{};
        if (GetClientRect(hwnd_, &client)) layoutControls(client.right, client.bottom);
        return 0;
    }
    case WM_NOTIFY:
        if (reinterpret_cast<const NMHDR*>(lParam)->idFrom == IdTabs &&
            reinterpret_cast<const NMHDR*>(lParam)->code == TCN_SELCHANGE) {
            setActiveTab(TabCtrl_GetCurSel(tabs_));
            return 0;
        }
        break;
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int notification = HIWORD(wParam);
        if (notification == BN_CLICKED) {
            if (id == IdColor) chooseTextColor();
            else if (id == IdExcludedAdd) addExcludedArea();
            else if (id == IdExcludedRemove) removeSelectedExcludedArea();
            else if (id == IdExcludedClear) clearExcludedAreas();
            else if (id == IdReset && onReset_) onReset_();
            else if (id == IdPreset && onPreset_) onPreset_();
            else if (id == IdPosition && onPositioning_) onPositioning_();
            else if (id == IdStatistics && onStatistics_) onStatistics_();
            else if (id == IdClose) hide();
            else applyFromControls();
        } else if (notification == CBN_SELCHANGE || notification == EN_KILLFOCUS) {
            applyFromControls();
        }
        return 0;
    }
    case WM_HSCROLL:
        switch (LOWORD(wParam)) {
        case TB_THUMBTRACK:
            sliderTracking_ = true;
            schedulePreviewApply();
            break;
        case TB_THUMBPOSITION:
        case TB_ENDTRACK:
            sliderTracking_ = false;
            KillTimer(hwnd_, kPreviewApplyTimer);
            applyFromControls(true);
            break;
        default:
            applyFromControls(true);
            break;
        }
        return 0;
    case WM_TIMER:
        if (wParam == kPreviewApplyTimer) {
            KillTimer(hwnd_, kPreviewApplyTimer);
            if (sliderTracking_) applyFromControls(false);
            return 0;
        }
        break;
    case WM_VSCROLL: {
        SCROLLINFO info{sizeof(SCROLLINFO), SIF_ALL};
        GetScrollInfo(hwnd_, SB_VERT, &info);
        int next = verticalOffset_;
        switch (LOWORD(wParam)) {
        case SB_LINEUP: next -= scale(32); break;
        case SB_LINEDOWN: next += scale(32); break;
        case SB_PAGEUP: next -= static_cast<int>(info.nPage); break;
        case SB_PAGEDOWN: next += static_cast<int>(info.nPage); break;
        case SB_THUMBTRACK: next = info.nTrackPos; break;
        case SB_TOP: next = 0; break;
        case SB_BOTTOM: next = scrollMaximum_; break;
        default: break;
        }
        setScrollOffset(next);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        scrollBy(delta > 0 ? -scale(48) : scale(48));
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_PRIOR || wParam == VK_NEXT) {
            scrollBy(wParam == VK_PRIOR ? -scale(240) : scale(240));
            return 0;
        }
        break;
    case WM_CLOSE:
        if (sliderTracking_) applyFromControls(true);
        sliderTracking_ = false;
        KillTimer(hwnd_, kPreviewApplyTimer);
        hide();
        return 0;
    case WM_NCDESTROY:
        KillTimer(hwnd_, kPreviewApplyTimer);
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

} // namespace aoc::platform

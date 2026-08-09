#include "aoc/platform/settings_window.h"

#include "aoc/core/geometry.h"
#include "aoc/platform/win32_ui.h"

#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <limits>
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
    IdIntervalHours,
    IdMicroShiftEnabled = 1013,
    IdMicroShiftDistance = 1014,
    IdEdgeMargin = 1015,
    IdAllowedPreset = 1016,
    IdAllowedLeft = 1017,
    IdAllowedTop = 1018,
    IdAllowedRight = 1019,
    IdAllowedBottom = 1020,
    IdIntervalMinutes = 1021,
    IdIntervalSeconds = 1022,
    IdMicroShiftCount = 1023,
    IdLocalAreaRadius = 1024,
    IdMonitorMode = 1030,
    IdFullscreen = 1031,
    IdStartup = 1032,
    IdHotkey = 1033,
    IdReset = 1034,
    IdPreset = 1035,
    IdPosition = 1036,
    IdStatistics = 1037,
    IdClose = 1038,
};

constexpr UINT kFinishShowSyncMessage = WM_APP + 0x3A;

constexpr COLORREF rgb(unsigned red, unsigned green, unsigned blue) noexcept {
    return static_cast<COLORREF>(red | (green << 8U) | (blue << 16U));
}

constexpr COLORREF kShellColor = rgb(246, 247, 251);
// One continuous surface avoids the inset white card/gray-frame effect from
// the native tab control and page host.
constexpr COLORREF kSurfaceColor = kShellColor;
constexpr COLORREF kTextColor = rgb(24, 27, 37);
constexpr COLORREF kMutedTextColor = rgb(102, 112, 133);
constexpr COLORREF kBorderColor = rgb(217, 221, 231);
constexpr COLORREF kAccentColor = rgb(91, 76, 245);
constexpr COLORREF kAccentPressedColor = rgb(72, 57, 222);
constexpr COLORREF kAccentSoftColor = rgb(239, 237, 255);
constexpr COLORREF kDisabledColor = rgb(242, 244, 247);

HBRUSH stockBrush(HDC dc, COLORREF color) {
    SetDCBrushColor(dc, color);
    return static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
}

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
    if (end == buffer || !std::isfinite(value)) return fallback;
    while (*end == L' ' || *end == L'\t') ++end;
    return *end == L'\0' ? value : fallback;
}

int readBoundedInteger(HWND control, int fallback, int minimum, int maximum) {
    wchar_t buffer[128]{};
    if (!control || GetWindowTextW(control, buffer, static_cast<int>(std::size(buffer))) == 0) return fallback;
    wchar_t* end = nullptr;
    const long long value = wcstoll(buffer, &end, 10);
    if (end == buffer) return fallback;
    while (*end == L' ' || *end == L'\t') ++end;
    if (*end != L'\0') return fallback;
    return static_cast<int>(std::clamp<long long>(value, minimum, maximum));
}

bool nearlyEqual(double first, double second) { return std::abs(first - second) < 0.005; }

int CALLBACK collectFontFamily(const LOGFONTW* font, const TEXTMETRICW*, DWORD, LPARAM data) {
    if (!font || font->lfFaceName[0] == L'@' || !data) return 1;
    auto* families = reinterpret_cast<std::vector<std::wstring>*>(data);
    families->emplace_back(font->lfFaceName);
    return 1;
}

} // namespace

SettingsWindow::~SettingsWindow() {
    if (hwnd_) DestroyWindow(hwnd_);
}

bool SettingsWindow::create(HINSTANCE instance, HWND owner, ApplyCallback onApply,
                            SimpleCallback onPositioning, SimpleCallback onStatistics) {
    if (hwnd_) return true;
    instance_ = instance;
    owner_ = owner;
    onApply_ = std::move(onApply);
    onPositioning_ = std::move(onPositioning);
    onStatistics_ = std::move(onStatistics);
    WNDCLASSEXW windowClass{sizeof(WNDCLASSEXW)};
    windowClass.hInstance = instance_;
    windowClass.lpfnWndProc = &SettingsWindow::windowProc;
    windowClass.lpszClassName = kSettingsClass;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = loadApplicationIcon(instance_, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    windowClass.hIconSm = loadApplicationIcon(instance_, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    // The controller is HWND_MESSAGE-only, so it cannot own a normal desktop window
    // reliably. Keep it as the event sink, but create Settings as an independent
    // top-level window so it can be activated, enumerated, and used at every DPI.
    hwnd_ = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, kSettingsClass, L"Adaptive OLED Clock Settings",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_VSCROLL,
                            CW_USEDEFAULT, CW_USEDEFAULT, 820, 740, nullptr, nullptr, instance_, this);
    return hwnd_ != nullptr;
}

int SettingsWindow::scale(int value) const noexcept {
    return scaleDip(value, dpi_);
}

void SettingsWindow::loadInstalledFonts() {
    installedFonts_.clear();
    HDC dc = GetDC(hwnd_);
    if (dc) {
        LOGFONTW query{};
        query.lfCharSet = DEFAULT_CHARSET;
        EnumFontFamiliesExW(dc, &query, &collectFontFamily,
                            reinterpret_cast<LPARAM>(&installedFonts_), 0);
        ReleaseDC(hwnd_, dc);
    }
    std::sort(installedFonts_.begin(), installedFonts_.end(), [](const auto& first, const auto& second) {
        return _wcsicmp(first.c_str(), second.c_str()) < 0;
    });
    installedFonts_.erase(std::unique(installedFonts_.begin(), installedFonts_.end(),
                                      [](const auto& first, const auto& second) {
                                          return _wcsicmp(first.c_str(), second.c_str()) == 0;
                                      }),
                          installedFonts_.end());
    if (installedFonts_.empty()) installedFonts_.push_back(L"Segoe UI");
}

void SettingsWindow::markPendingEdit(HWND control) {
    if (syncing_ || !control) return;
    pendingEdits_.insert(control);
    dirty_ = true;
    setText(statusLabel_, L"Unsaved changes — select Apply, or press Enter in a number box.");
    RedrawWindow(control, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
}

void SettingsWindow::clearPendingEdits() {
    const auto controls = pendingEdits_;
    pendingEdits_.clear();
    for (HWND control : controls) {
        if (IsWindow(control)) RedrawWindow(control, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME);
    }
}

void SettingsWindow::setScrollOffset(int offset) {
    const int next = std::clamp(offset, 0, scrollMaximum_);
    if (next == verticalOffset_) return;
    verticalOffset_ = next;
    RECT client{};
    if (hwnd_ && GetClientRect(hwnd_, &client)) layoutControls(client.right, client.bottom);
}

void SettingsWindow::scrollBy(int amount) { setScrollOffset(verticalOffset_ + amount); }

LRESULT CALLBACK SettingsWindow::pageControlSubclassProc(HWND control, UINT message, WPARAM wParam,
                                                         LPARAM lParam, UINT_PTR subclassId,
                                                         DWORD_PTR refData) {
    (void)subclassId;
    auto* self = reinterpret_cast<SettingsWindow*>(refData);
    if (self) {
        if (control == self->pageHost_ &&
            (message == WM_COMMAND || message == WM_CTLCOLORSTATIC ||
             message == WM_CTLCOLORBTN || message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX)) {
            return SendMessageW(self->hwnd_, message, wParam, lParam);
        }
        if (message == WM_MOUSEWHEEL) {
            wchar_t className[32]{};
            GetClassNameW(control, className, static_cast<int>(std::size(className)));
            if (wcscmp(className, L"ComboBox") == 0 || wcscmp(className, L"ListBox") == 0) {
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
        wchar_t className[32]{};
        GetClassNameW(control, className, static_cast<int>(std::size(className)));
        const bool edit = wcscmp(className, L"Edit") == 0;
        if (edit && message == WM_GETDLGCODE && wParam == VK_RETURN) {
            return DLGC_WANTMESSAGE;
        }
        if (edit && message == WM_KEYDOWN && wParam == VK_RETURN) {
            self->applyFromControls(true);
            return 0;
        }
        if (edit && (message == WM_SETFOCUS || message == WM_KILLFOCUS)) {
            const LRESULT result = DefSubclassProc(control, message, wParam, lParam);
            RedrawWindow(control, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME);
            return result;
        }
        if (edit && message == WM_NCPAINT) {
            const LRESULT result = DefSubclassProc(control, message, wParam, lParam);
            if (GetFocus() == control || self->pendingEdits_.contains(control)) {
                HDC dc = GetWindowDC(control);
                if (dc) {
                    RECT bounds{};
                    GetWindowRect(control, &bounds);
                    OffsetRect(&bounds, -bounds.left, -bounds.top);
                    HPEN pen = CreatePen(PS_SOLID, std::max(1, self->scale(2)), kAccentColor);
                    HGDIOBJ previousPen = SelectObject(dc, pen);
                    HGDIOBJ previousBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
                    Rectangle(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
                    SelectObject(dc, previousBrush);
                    SelectObject(dc, previousPen);
                    DeleteObject(pen);
                    ReleaseDC(control, dc);
                }
            }
            return result;
        }
    }
    return DefSubclassProc(control, message, wParam, lParam);
}

LRESULT CALLBACK SettingsWindow::tabControlSubclassProc(HWND control, UINT message, WPARAM wParam,
                                                        LPARAM lParam, UINT_PTR subclassId,
                                                        DWORD_PTR refData) {
    (void)subclassId;
    auto* self = reinterpret_cast<SettingsWindow*>(refData);
    if (!self) return DefSubclassProc(control, message, wParam, lParam);
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(control, &paint);
        self->paintTabs(dc);
        EndPaint(control, &paint);
        return 0;
    }
    if (message == WM_PRINTCLIENT) {
        self->paintTabs(reinterpret_cast<HDC>(wParam));
        return 0;
    }
    if (message == WM_ERASEBKGND) return 1;
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
    dirty_ = false;
    clearPendingEdits();
    verticalOffset_ = 0;
    // Keep activation/layout notifications from reading the creation defaults
    // back into the loaded model before the final post-show synchronization.
    syncing_ = true;
    const RECT bounds = centeredWindowRect(owner_, hwnd_, 820, 740, dpi_);
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

void SettingsWindow::syncApplied(const core::Settings& settings,
                                 const std::vector<core::MonitorInfo>& monitors,
                                 bool force) {
    if (!hwnd_ || (!force && dirty_)) return;
    settings_ = settings;
    monitors_ = monitors;
    dirty_ = false;
    clearPendingEdits();
    syncToControls();
    setText(statusLabel_, L"Edit a value, then select Apply. Enter also applies number fields.");
}

void SettingsWindow::hide() {
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

bool SettingsWindow::createControls() {
    dpi_ = GetDpiForWindow(hwnd_);
    if (dpi_ == 0) dpi_ = 96;
    controlFont_.reset(createControlFont(dpi_));
    loadInstalledFonts();
    titleFont_.reset(CreateFontW(-scale(24), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                 L"Segoe UI Variable Display"));
    if (!controlFont_) controlCreationFailed_ = true;
    titleLabel_ = addControl(-1, SS_LEFT | SS_NOPREFIX, L"STATIC", L"Settings", 0);
    subtitleLabel_ = addControl(-1, SS_LEFT | SS_NOPREFIX, L"STATIC",
                                L"Choose how the clock looks and when it moves.", 0);
    if (titleLabel_ && titleFont_) setControlFont(titleLabel_, titleFont_.get());

    tabs_ = CreateWindowExW(0, WC_TABCONTROLW, L"",
                            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP |
                                TCS_OWNERDRAWFIXED | TCS_FIXEDWIDTH | TCS_HOTTRACK,
                            scale(12), scale(12), scale(836), scale(600), hwnd_,
                            reinterpret_cast<HMENU>(IdTabs), instance_, nullptr);
    if (tabs_) {
        allControls_.push_back(tabs_);
        setControlFont(tabs_, controlFont_.get());
        SetWindowSubclass(tabs_, &SettingsWindow::tabControlSubclassProc, 2,
                          reinterpret_cast<DWORD_PTR>(this));
        for (const wchar_t* title : {L"Clock", L"Movement", L"Display & Windows"}) {
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

    statusLabel_ = addControl(-1, SS_LEFT | SS_NOPREFIX, L"STATIC",
                              L"Edit a value, then select Apply. Enter also applies number fields.", 0);
    resetButton_ = addControl(-1, BS_OWNERDRAW | WS_TABSTOP, L"BUTTON", L"Reset", IdReset);
    presetButton_ = addControl(-1, BS_OWNERDRAW | WS_TABSTOP, L"BUTTON", L"OLED preset", IdPreset);
    positioningButton_ = addControl(-1, BS_OWNERDRAW | WS_TABSTOP, L"BUTTON", L"Position clock", IdPosition);
    statisticsButton_ = addControl(-1, BS_OWNERDRAW | WS_TABSTOP, L"BUTTON", L"Movement history", IdStatistics);
    closeButton_ = addControl(-1, BS_OWNERDRAW | WS_TABSTOP, L"BUTTON", L"Apply", IdClose);
    applyVisualTheme();
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
    for (const auto& value : installedFonts_)
        SendMessageW(fontFamily_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value.c_str()));
    fontWeightLabel_ = addLabel(0, L"Font weight");
    fontWeight_ = addControl(0, CBS_DROPDOWNLIST | WS_VSCROLL, L"COMBOBOX", L"", IdFontWeight);
    for (const wchar_t* value : {L"Normal", L"Semibold"})
        SendMessageW(fontWeight_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    fontSizeLabel_ = addLabel(0, L"Font size");
    fontSize_ = addControl(0, ES_NUMBER | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, L"EDIT", L"32", IdFontSize);
    fontSizeValue_ = addControl(0, SS_LEFT, L"STATIC", L"8 to 128", 0);
    opacityLabel_ = addLabel(0, L"Normal opacity (%)");
    opacity_ = addControl(0, ES_NUMBER | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, L"EDIT", L"80", IdOpacity);
    opacityValue_ = addControl(0, SS_LEFT, L"STATIC", L"0 to 100", 0);
    colorLabel_ = addLabel(0, L"Clock color");
    colorButton_ = addControl(0, BS_OWNERDRAW | WS_TABSTOP, L"BUTTON", L"Choose clock color...", IdColor);
    boostOpacityLabel_ = addLabel(0, L"Temporary brightness (%)");
    boostOpacity_ = addControl(0, ES_NUMBER | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, L"EDIT", L"100", IdBoostOpacity);
    boostOpacityValue_ = addControl(0, SS_LEFT, L"STATIC", L"0 to 100", 0);
    boostDurationLabel_ = addLabel(0, L"Boost duration");
    boostDuration_ = addControl(0, CBS_DROPDOWNLIST | WS_VSCROLL, L"COMBOBOX", L"", IdBoostDuration);
    for (const wchar_t* value : {L"5 seconds", L"10 seconds", L"15 seconds", L"30 seconds", L"60 seconds", L"120 seconds"})
        SendMessageW(boostDuration_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    return !controlCreationFailed_;
}

bool SettingsWindow::createMovementPage() {
    movementModeLabel_ = addLabel(1, L"Movement mode");
    movementMode_ = addControl(1, CBS_DROPDOWNLIST | WS_VSCROLL, L"COMBOBOX", L"", IdMovementMode);
    for (const wchar_t* value : {L"Edge only", L"Four corners", L"Whole screen", L"Local area"})
        SendMessageW(movementMode_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    localAreaRadiusLabel_ = addLabel(1, L"Local movement radius (px)");
    localAreaRadius_ = addControl(1, ES_NUMBER | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP,
                                  L"EDIT", L"100", IdLocalAreaRadius);
    localAreaHelp_ = addControl(1, SS_LEFT | SS_NOPREFIX, L"STATIC",
                                L"Position clock sets the center used by Local area.", 0);
    intervalLabel_ = addLabel(1, L"Time between movements");
    intervalHoursLabel_ = addControl(1, SS_CENTER, L"STATIC", L"Hours", 0);
    intervalMinutesLabel_ = addControl(1, SS_CENTER, L"STATIC", L"Minutes", 0);
    intervalSecondsLabel_ = addControl(1, SS_CENTER, L"STATIC", L"Seconds", 0);
    intervalHours_ = addControl(1, ES_NUMBER | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP,
                                L"EDIT", L"1", IdIntervalHours);
    intervalMinutes_ = addControl(1, ES_NUMBER | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP,
                                  L"EDIT", L"0", IdIntervalMinutes);
    intervalSeconds_ = addControl(1, ES_NUMBER | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP,
                                  L"EDIT", L"0", IdIntervalSeconds);
    microShiftEnabled_ = addControl(1, BS_AUTOCHECKBOX | WS_TABSTOP, L"BUTTON",
                                    L"Use small shifts between scheduled movements", IdMicroShiftEnabled);
    microShiftCountLabel_ = addLabel(1, L"Number of small shifts");
    microShiftCount_ = addControl(1, ES_NUMBER | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP,
                                  L"EDIT", L"3", IdMicroShiftCount);
    microShiftDistanceLabel_ = addLabel(1, L"Small shift distance (px)");
    microShiftDistance_ = addControl(1, ES_NUMBER | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP,
                                     L"EDIT", L"3", IdMicroShiftDistance);
    edgeMarginLabel_ = addLabel(1, L"Gap from screen edge (px)");
    edgeMargin_ = addControl(1, ES_NUMBER | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, L"EDIT", L"0", IdEdgeMargin);
    allowedPresetLabel_ = addLabel(1, L"Allowed movement area");
    allowedPreset_ = addControl(1, CBS_DROPDOWNLIST | WS_VSCROLL, L"COMBOBOX", L"", IdAllowedPreset);
    for (const wchar_t* value : {L"Entire usable area", L"Center 80%", L"Center 60%", L"Custom"})
        SendMessageW(allowedPreset_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    allowedAreaHelp_ = addControl(1, SS_LEFT | SS_NOPREFIX, L"STATIC",
                                  L"Custom area (%)", 0);
    allowedLeftLabel_ = addControl(1, SS_CENTER, L"STATIC", L"Left", 0);
    allowedTopLabel_ = addControl(1, SS_CENTER, L"STATIC", L"Top", 0);
    allowedRightLabel_ = addControl(1, SS_CENTER, L"STATIC", L"Right", 0);
    allowedBottomLabel_ = addControl(1, SS_CENTER, L"STATIC", L"Bottom", 0);
    allowedLeft_ = addControl(1, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, L"EDIT", L"0", IdAllowedLeft);
    allowedTop_ = addControl(1, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, L"EDIT", L"0", IdAllowedTop);
    allowedRight_ = addControl(1, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, L"EDIT", L"100", IdAllowedRight);
    allowedBottom_ = addControl(1, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, L"EDIT", L"100", IdAllowedBottom);
    return !controlCreationFailed_;
}

bool SettingsWindow::createDisplayPage() {
    monitorModeLabel_ = addLabel(2, L"Monitor");
    monitorMode_ = addControl(2, CBS_DROPDOWNLIST | WS_VSCROLL, L"COMBOBOX", L"", IdMonitorMode);
    fullscreen_ = addControl(2, BS_AUTOCHECKBOX | WS_TABSTOP, L"BUTTON", L"Hide while a fullscreen app is active", IdFullscreen);
    startup_ = addControl(2, BS_AUTOCHECKBOX | WS_TABSTOP, L"BUTTON", L"Launch at Windows sign-in", IdStartup);
    hotkey_ = addControl(2, BS_AUTOCHECKBOX | WS_TABSTOP, L"BUTTON", L"Enable global Ctrl+Alt+C visibility toggle", IdHotkey);
    displayHelp_ = addControl(2, SS_LEFT | SS_NOPREFIX, L"STATIC",
                              L"The clock follows your primary monitor unless you choose another display.",
                              0);
    return !controlCreationFailed_;
}

void SettingsWindow::applyVisualTheme() {
    for (HWND control : allControls_) {
        if (control) SetWindowTheme(control, L"Explorer", nullptr);
    }

    // These attributes are ignored safely on older Windows versions. On Windows
    // 11 they align the non-client area with the light, rounded settings surface.
    constexpr DWORD kWindowCornerPreference = 33;
    constexpr DWORD kBorderColorAttribute = 34;
    constexpr DWORD kCaptionColorAttribute = 35;
    constexpr DWORD kTextColorAttribute = 36;
    const int roundedCorners = 2; // DWMWCP_ROUND
    const COLORREF caption = kShellColor;
    const COLORREF border = kBorderColor;
    const COLORREF text = kTextColor;
    (void)DwmSetWindowAttribute(hwnd_, static_cast<DWMWINDOWATTRIBUTE>(kWindowCornerPreference),
                                &roundedCorners, sizeof(roundedCorners));
    (void)DwmSetWindowAttribute(hwnd_, static_cast<DWMWINDOWATTRIBUTE>(kBorderColorAttribute),
                                &border, sizeof(border));
    (void)DwmSetWindowAttribute(hwnd_, static_cast<DWMWINDOWATTRIBUTE>(kCaptionColorAttribute),
                                &caption, sizeof(caption));
    (void)DwmSetWindowAttribute(hwnd_, static_cast<DWMWINDOWATTRIBUTE>(kTextColorAttribute),
                                &text, sizeof(text));
}

void SettingsWindow::drawButton(const DRAWITEMSTRUCT& item) const {
    HDC dc = item.hDC;
    RECT bounds = item.rcItem;
    const int id = GetDlgCtrlID(item.hwndItem);
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool primary = id == IdClose;

    COLORREF fill = primary ? kAccentColor : kSurfaceColor;
    COLORREF border = primary ? kAccentColor : kBorderColor;
    COLORREF text = primary ? kSurfaceColor : kTextColor;
    if (disabled) {
        fill = kDisabledColor;
        border = kBorderColor;
        text = kMutedTextColor;
    } else if (pressed) {
        fill = primary ? kAccentPressedColor : kAccentSoftColor;
        border = primary ? kAccentPressedColor : kAccentColor;
    }

    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, std::max(1, scale(1)), border);
    const HGDIOBJ previousBrush = SelectObject(dc, brush);
    const HGDIOBJ previousPen = SelectObject(dc, pen);
    const int radius = scale(8);
    RoundRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom, radius, radius);
    SelectObject(dc, previousPen);
    SelectObject(dc, previousBrush);
    DeleteObject(pen);
    DeleteObject(brush);

    wchar_t label[256]{};
    GetWindowTextW(item.hwndItem, label, static_cast<int>(std::size(label)));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, text);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(item.hwndItem, WM_GETFONT, 0, 0));
    const HGDIOBJ previousFont = font ? SelectObject(dc, font) : nullptr;

    RECT textBounds = bounds;
    if (id == IdColor) {
        const int swatchSize = scale(16);
        const int swatchLeft = bounds.left + scale(14);
        const int swatchTop = bounds.top + (bounds.bottom - bounds.top - swatchSize) / 2;
        const COLORREF swatchColor = RGB(settings_.textColor.r, settings_.textColor.g, settings_.textColor.b);
        HBRUSH swatchBrush = CreateSolidBrush(swatchColor);
        HPEN swatchPen = CreatePen(PS_SOLID, std::max(1, scale(1)), kBorderColor);
        const HGDIOBJ oldBrush = SelectObject(dc, swatchBrush);
        const HGDIOBJ oldPen = SelectObject(dc, swatchPen);
        Ellipse(dc, swatchLeft, swatchTop, swatchLeft + swatchSize, swatchTop + swatchSize);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(swatchPen);
        DeleteObject(swatchBrush);
        textBounds.left += scale(40);
        DrawTextW(dc, label, -1, &textBounds, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    } else {
        DrawTextW(dc, label, -1, &textBounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    if (previousFont) SelectObject(dc, previousFont);
    if ((item.itemState & ODS_FOCUS) != 0) {
        InflateRect(&bounds, -scale(4), -scale(4));
        DrawFocusRect(dc, &bounds);
    }
}

void SettingsWindow::drawTab(const DRAWITEMSTRUCT& item) const {
    HDC dc = item.hDC;
    RECT bounds = item.rcItem;
    const bool selected = static_cast<int>(item.itemID) == activeTab_;
    FillRect(dc, &bounds, stockBrush(dc, selected ? kAccentSoftColor : kSurfaceColor));

    if (selected) {
        RECT accent = bounds;
        accent.top = accent.bottom - scale(3);
        FillRect(dc, &accent, stockBrush(dc, kAccentColor));
    }

    wchar_t label[128]{};
    TCITEMW tab{TCIF_TEXT};
    tab.pszText = label;
    tab.cchTextMax = static_cast<int>(std::size(label));
    TabCtrl_GetItem(item.hwndItem, static_cast<int>(item.itemID), &tab);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, selected ? kAccentColor : kMutedTextColor);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(item.hwndItem, WM_GETFONT, 0, 0));
    const HGDIOBJ previousFont = font ? SelectObject(dc, font) : nullptr;
    DrawTextW(dc, label, -1, &bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (previousFont) SelectObject(dc, previousFont);
}

void SettingsWindow::paintTabs(HDC dc) const {
    if (!dc || !tabs_) return;
    RECT client{};
    if (!GetClientRect(tabs_, &client)) return;
    FillRect(dc, &client, stockBrush(dc, kSurfaceColor));

    const int count = std::max(1, TabCtrl_GetItemCount(tabs_));
    RECT nativeItem{};
    const int headerBottom = TabCtrl_GetItemRect(tabs_, 0, &nativeItem)
        ? std::clamp(static_cast<int>(nativeItem.bottom), 1, static_cast<int>(client.bottom))
        : std::min(scale(40), static_cast<int>(client.bottom));
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(tabs_, WM_GETFONT, 0, 0));
    const HGDIOBJ previousFont = font ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);

    for (int index = 0; index < count; ++index) {
        RECT bounds{client.left + index * (client.right - client.left) / count,
                    client.top,
                    client.left + (index + 1) * (client.right - client.left) / count,
                    headerBottom};
        const bool selected = index == activeTab_;
        FillRect(dc, &bounds, stockBrush(dc, selected ? kAccentSoftColor : kSurfaceColor));
        if (selected) {
            RECT accent = bounds;
            accent.top = accent.bottom - scale(3);
            FillRect(dc, &accent, stockBrush(dc, kAccentColor));
        }

        wchar_t label[128]{};
        TCITEMW item{TCIF_TEXT};
        item.pszText = label;
        item.cchTextMax = static_cast<int>(std::size(label));
        (void)TabCtrl_GetItem(tabs_, index, &item);
        SetTextColor(dc, selected ? kAccentColor : kMutedTextColor);
        DrawTextW(dc, label, -1, &bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    if (previousFont) SelectObject(dc, previousFont);
}

void SettingsWindow::layoutControls(int width, int height) {
    if (!tabs_ || !pageHost_) return;
    width = std::max(1, width);
    height = std::max(1, height);
    const int margin = std::min(scale(24), std::max(0, (width - 1) / 2));
    const int actionGap = scale(10);
    const int actionButtonHeight = scale(36);
    const int actionNoteHeight = scale(20);
    const int actionNoteGap = scale(8);
    const int actionAvailableWidth = std::max(1, width - 2 * margin);
    const int actionNaturalWidths[] = {88, 112, 120, 132, 88};
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
    footerTop_ = actionAreaTop - scale(16);
    const int headerTop = scale(18);
    const int tabsTop = scale(82);
    const int tabWidth = std::max(1, width - 2 * margin);
    const int tabHeight = std::max(1, footerTop_ - actionGap - tabsTop);
    MoveWindow(titleLabel_, margin, headerTop, tabWidth, scale(30), TRUE);
    MoveWindow(subtitleLabel_, margin, headerTop + scale(34), tabWidth, scale(20), TRUE);
    SendMessageW(tabs_, TCM_SETITEMSIZE, 0,
                 MAKELPARAM(std::max(1, (tabWidth - scale(6)) / 3), scale(38)));
    MoveWindow(tabs_, margin, tabsTop, tabWidth, tabHeight, TRUE);
    RECT page{};
    GetClientRect(tabs_, &page);
    TabCtrl_AdjustRect(tabs_, FALSE, &page);
    const int pageWidth = std::max(1, static_cast<int>(page.right - page.left));
    const int pageHeight = std::max(1, static_cast<int>(page.bottom - page.top));
    // The page host is a sibling of the tab control. Keep it explicitly above
    // the tab's page body; otherwise the tab can remain first in child Z-order
    // and paint over every page control even though they are visible and laid
    // out correctly.
    // Cover the tab control's page body completely. Leaving an inset exposes
    // the common control's gray frame around an otherwise white settings page.
    const int frameOverlap = scale(2);
    const int hostWidth = std::max(1, pageWidth + frameOverlap * 2);
    const int hostHeight = std::max(1, pageHeight + frameOverlap * 2);
    SetWindowPos(pageHost_, HWND_TOP, margin + page.left - frameOverlap,
                  tabsTop + page.top - scale(1),
                  hostWidth, hostHeight, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    const int left = 0;
    const int contentWidth = hostWidth;
    const int contentHeightViewport = hostHeight;
    const bool compact = contentWidth < scale(600);
    const int fieldX = compact ? 0 : scale(236);
    const int fieldWidth = compact ? contentWidth : std::max(scale(260), contentWidth - fieldX);
    const int rowHeight = scale(30);
    int contentHeight = 0;

    for (auto& pageState : pages_) {
        for (HWND control : pageState.controls) ShowWindow(control, SW_HIDE);
    }
    auto show = [&](HWND control, int x, int y, int w, int h, int pageIndex) {
        if (!control || (pageIndex >= 0 && pageIndex != activeTab_)) return;
        const int adjustedY = pageIndex >= 0 ? y - verticalOffset_ : y;
        const bool inPageViewport = pageIndex < 0 || (adjustedY + h > 0 && adjustedY < contentHeightViewport);
        wchar_t className[32]{};
        GetClassNameW(control, className, static_cast<int>(std::size(className)));
        // A combo box's window height also owns the popup-list extent. Keeping
        // only the collapsed row height here makes the arrow appear responsive
        // while leaving no usable list to display.
        const int layoutHeight = wcscmp(className, L"ComboBox") == 0 ? scale(240) : h;
        MoveWindow(control, x, adjustedY, w, layoutHeight, TRUE);
        ShowWindow(control, inPageViewport ? SW_SHOW : SW_HIDE);
    };
    auto label = [&](HWND control, int y, int pageIndex) {
        show(control, left, y, scale(212), rowHeight, pageIndex);
    };
    auto field = [&](HWND control, int y, int w = -1, int h = -1, int pageIndex = -1) {
        const int page = pageIndex < 0 ? activeTab_ : pageIndex;
        show(control, fieldX, y, w < 0 ? fieldWidth : scale(w), h < 0 ? rowHeight : scale(h), page);
    };
    auto labelAt = [&](HWND control, int x, int y, int w, int h) {
        show(control, x, y, w, h, activeTab_);
    };
    auto compactLabelField = [&](HWND labelControl, HWND fieldControl, int& y, int fieldHeightDip = 30) {
        show(labelControl, 0, y, contentWidth, rowHeight, activeTab_);
        y += rowHeight + scale(4);
        show(fieldControl, 0, y, contentWidth, scale(fieldHeightDip), activeTab_);
        y += scale(fieldHeightDip + 10);
    };
    auto compactCheck = [&](HWND control, int& y) {
        show(control, 0, y, contentWidth, scale(28), activeTab_);
        y += scale(38);
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
            label(fontSizeLabel_, y, 0); show(fontSize_, fieldX, y, scale(120), rowHeight, 0); labelAt(fontSizeValue_, fieldX + scale(132), y, scale(120), rowHeight); y += scale(38);
            label(opacityLabel_, y, 0); show(opacity_, fieldX, y, scale(120), rowHeight, 0); labelAt(opacityValue_, fieldX + scale(132), y, scale(120), rowHeight); y += scale(38);
            label(colorLabel_, y, 0); field(colorButton_, y, -1, 30, 0); y += scale(38);
            label(boostOpacityLabel_, y, 0); show(boostOpacity_, fieldX, y, scale(120), rowHeight, 0); labelAt(boostOpacityValue_, fieldX + scale(132), y, scale(120), rowHeight); y += scale(38);
            label(boostDurationLabel_, y, 0); field(boostDuration_, y, 220, 30, 0);
            contentHeight = y + rowHeight;
        } else {
            compactLabelField(timeFormatLabel_, timeFormat_, y);
            compactCheck(showSeconds_, y);
            compactCheck(showDate_, y);
            compactCheck(showAmPm_, y);
            compactLabelField(fontFamilyLabel_, fontFamily_, y);
            compactLabelField(fontWeightLabel_, fontWeight_, y);
            compactLabelField(fontSizeLabel_, fontSize_, y);
            compactLabelField(opacityLabel_, opacity_, y);
            compactLabelField(colorLabel_, colorButton_, y);
            compactLabelField(boostOpacityLabel_, boostOpacity_, y);
            compactLabelField(boostDurationLabel_, boostDuration_, y);
            contentHeight = y;
        }
    } else if (activeTab_ == 1) {
        int y = scale(18);
        if (!compact) {
            label(movementModeLabel_, y, 1); field(movementMode_, y, -1, 30, 1); y += scale(42);
            label(localAreaRadiusLabel_, y, 1);
            show(localAreaRadius_, fieldX, y, scale(120), rowHeight, 1);
            labelAt(localAreaHelp_, fieldX + scale(132), y, std::max(1, contentWidth - fieldX - scale(132)), rowHeight);
            y += scale(42);
            const int durationBoxWidth = scale(70);
            const int durationGap = scale(8);
            label(intervalLabel_, y + scale(18), 1);
            for (const auto& [index, control] : std::array<std::pair<int, HWND>, 3>{
                     std::pair{0, intervalHoursLabel_}, std::pair{1, intervalMinutesLabel_},
                     std::pair{2, intervalSecondsLabel_}}) {
                show(control, fieldX + index * (durationBoxWidth + durationGap), y,
                     durationBoxWidth, scale(18), 1);
            }
            y += scale(18);
            for (const auto& [index, control] : std::array<std::pair<int, HWND>, 3>{
                     std::pair{0, intervalHours_}, std::pair{1, intervalMinutes_},
                     std::pair{2, intervalSeconds_}}) {
                show(control, fieldX + index * (durationBoxWidth + durationGap), y,
                     durationBoxWidth, rowHeight, 1);
            }
            y += scale(42);
            field(microShiftEnabled_, y, -1, 28, 1); y += scale(34);
            label(microShiftCountLabel_, y, 1);
            show(microShiftCount_, fieldX, y, scale(120), rowHeight, 1);
            y += scale(40);
            label(microShiftDistanceLabel_, y, 1);
            show(microShiftDistance_, fieldX, y, scale(120), rowHeight, 1);
            y += scale(40);
            label(edgeMarginLabel_, y, 1); show(edgeMargin_, fieldX, y, scale(120), rowHeight, 1); y += scale(40);
            label(allowedPresetLabel_, y, 1); field(allowedPreset_, y, -1, 30, 1); y += scale(40);
            labelAt(allowedAreaHelp_, left, y, contentWidth, rowHeight); y += scale(30);
            const int boxWidth = scale(64);
            const int boxGap = scale(8);
            for (const auto& [index, control] : std::array<std::pair<int, HWND>, 4>{
                     std::pair{0, allowedLeftLabel_}, std::pair{1, allowedTopLabel_},
                     std::pair{2, allowedRightLabel_}, std::pair{3, allowedBottomLabel_}}) {
                show(control, left + index * (boxWidth + boxGap), y, boxWidth, scale(18), 1);
            }
            y += scale(18);
            for (const auto& [index, control] : std::array<std::pair<int, HWND>, 4>{
                     std::pair{0, allowedLeft_}, std::pair{1, allowedTop_},
                     std::pair{2, allowedRight_}, std::pair{3, allowedBottom_}}) {
                show(control, left + index * (boxWidth + boxGap), y, boxWidth, rowHeight, 1);
            }
            contentHeight = y + scale(38);
        } else {
            compactLabelField(movementModeLabel_, movementMode_, y);
            compactLabelField(localAreaRadiusLabel_, localAreaRadius_, y);
            show(localAreaHelp_, 0, y, contentWidth, rowHeight, 1); y += scale(34);
            show(intervalLabel_, 0, y, contentWidth, rowHeight, 1); y += scale(30);
            const int compactGap = scale(8);
            const int compactBoxWidth = std::max(1, (contentWidth - compactGap * 2) / 3);
            for (const auto& [index, control] : std::array<std::pair<int, HWND>, 3>{
                     std::pair{0, intervalHoursLabel_}, std::pair{1, intervalMinutesLabel_},
                     std::pair{2, intervalSecondsLabel_}}) {
                show(control, index * (compactBoxWidth + compactGap), y,
                     compactBoxWidth, scale(18), 1);
            }
            y += scale(18);
            for (const auto& [index, control] : std::array<std::pair<int, HWND>, 3>{
                     std::pair{0, intervalHours_}, std::pair{1, intervalMinutes_},
                     std::pair{2, intervalSeconds_}}) {
                show(control, index * (compactBoxWidth + compactGap), y,
                     compactBoxWidth, rowHeight, 1);
            }
            y += scale(38);
            compactCheck(microShiftEnabled_, y);
            compactLabelField(microShiftCountLabel_, microShiftCount_, y);
            compactLabelField(microShiftDistanceLabel_, microShiftDistance_, y);
            compactLabelField(edgeMarginLabel_, edgeMargin_, y);
            compactLabelField(allowedPresetLabel_, allowedPreset_, y);
            show(allowedAreaHelp_, 0, y, contentWidth, rowHeight, 1); y += scale(34);
            compactLabelField(allowedLeftLabel_, allowedLeft_, y);
            compactLabelField(allowedTopLabel_, allowedTop_, y);
            compactLabelField(allowedRightLabel_, allowedRight_, y);
            compactLabelField(allowedBottomLabel_, allowedBottom_, y);
            contentHeight = y;
        }
    } else if (activeTab_ == 2) {
        int y = scale(18);
        if (!compact) {
            label(monitorModeLabel_, y, 2); field(monitorMode_, y, -1, 30, 2); y += scale(48);
            field(fullscreen_, y, -1, 28, 2); y += scale(36);
            field(startup_, y, -1, 28, 2); y += scale(36);
            field(hotkey_, y, -1, 28, 2); y += scale(52);
            labelAt(displayHelp_, left, y, contentWidth, scale(60));
            contentHeight = y + scale(60);
        } else {
            compactLabelField(monitorModeLabel_, monitorMode_, y);
            compactCheck(fullscreen_, y);
            compactCheck(startup_, y);
            compactCheck(hotkey_, y);
            show(displayHelp_, 0, y, contentWidth, scale(90), 2);
            contentHeight = y + scale(90);
        }
    }

    const int nextMaximum = std::max(0, contentHeight - contentHeightViewport);
    const int nextOffset = std::clamp(verticalOffset_, 0, nextMaximum);
    scrollMaximum_ = nextMaximum;
    ShowScrollBar(hwnd_, SB_VERT, scrollMaximum_ > 0);
    SCROLLINFO scrollInfo{sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE | SIF_POS};
    scrollInfo.nMin = 0;
    scrollInfo.nMax = std::max(0, contentHeight - 1);
    scrollInfo.nPage = static_cast<UINT>(contentHeightViewport);
    scrollInfo.nPos = nextOffset;
    SetScrollInfo(hwnd_, SB_VERT, &scrollInfo, TRUE);
    if (nextOffset != verticalOffset_) {
        verticalOffset_ = nextOffset;
        layoutControls(width, height);
        return;
    }

    int actionX = margin;
    show(statusLabel_, margin, actionAreaTop, actionAvailableWidth, actionNoteHeight, -1);
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
    actionButton(resetButton_, 88);
    actionButton(presetButton_, 112);
    actionButton(positioningButton_, 120);
    actionButton(statisticsButton_, 132);
    actionButton(closeButton_, 88);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::setActiveTab(int tab) {
    activeTab_ = std::clamp(tab, 0, 2);
    verticalOffset_ = 0;
    if (tabs_) TabCtrl_SetCurSel(tabs_, activeTab_);
    if (tabs_) InvalidateRect(tabs_, nullptr, FALSE);
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
    setText(fontSize_, std::to_wstring(static_cast<int>(std::lround(settings_.fontSizeDip))));
    setText(opacity_, std::to_wstring(static_cast<int>(std::lround(settings_.opacity * 100.0))));
    setText(boostOpacity_, std::to_wstring(static_cast<int>(std::lround(settings_.boostOpacity * 100.0))));
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
    for (const auto& value : installedFonts_) {
        const LRESULT index = SendMessageW(fontFamily_, CB_ADDSTRING, 0,
                                            reinterpret_cast<LPARAM>(value.c_str()));
        if (_wcsicmp(selectedFamily.c_str(), value.c_str()) == 0) familySelection = static_cast<int>(index);
    }
    if (familySelection < 0 && !selectedFamily.empty()) {
        familySelection = static_cast<int>(SendMessageW(fontFamily_, CB_ADDSTRING, 0,
                                                         reinterpret_cast<LPARAM>(selectedFamily.c_str())));
    }
    SendMessageW(fontFamily_, CB_SETCURSEL, std::max(0, familySelection), 0);
    setText(colorButton_, L"Clock color: " + std::to_wstring(settings_.textColor.r) + L", " +
                           std::to_wstring(settings_.textColor.g) + L", " + std::to_wstring(settings_.textColor.b));
}

void SettingsWindow::syncMovementPage() {
    SendMessageW(movementMode_, CB_SETCURSEL,
                 settings_.movementMode == core::MovementMode::EdgeOnly ? 0 :
                 settings_.movementMode == core::MovementMode::FourCorners ? 1 :
                 settings_.movementMode == core::MovementMode::WholeScreen ? 2 : 3, 0);
    setText(localAreaRadius_, std::to_wstring(settings_.localAreaRadiusPx));
    const int hours = settings_.movementIntervalSeconds / 3600;
    const int minutes = (settings_.movementIntervalSeconds / 60) % 60;
    const int seconds = settings_.movementIntervalSeconds % 60;
    setText(intervalHours_, std::to_wstring(hours));
    setText(intervalMinutes_, std::to_wstring(minutes));
    setText(intervalSeconds_, std::to_wstring(seconds));
    SendMessageW(microShiftEnabled_, BM_SETCHECK,
                 settings_.microShiftEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    setText(microShiftCount_, std::to_wstring(settings_.microShiftCount));
    setText(microShiftDistance_, std::to_wstring(settings_.microShiftDistancePx));
    EnableWindow(microShiftCount_, settings_.microShiftEnabled ? TRUE : FALSE);
    EnableWindow(microShiftDistance_, settings_.microShiftEnabled ? TRUE : FALSE);
    setText(edgeMargin_, std::to_wstring(static_cast<int>(std::lround(settings_.edgeMarginDip))));
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
    updateMovementEditorState();
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
    syncing_ = false;
}

void SettingsWindow::updateMovementEditorState() {
    const bool custom = allowedPreset_ && SendMessageW(allowedPreset_, CB_GETCURSEL, 0, 0) == 3;
    for (HWND control : {allowedLeft_, allowedTop_, allowedRight_, allowedBottom_}) {
        if (control) EnableWindow(control, custom ? TRUE : FALSE);
    }
    if (allowedAreaHelp_) InvalidateRect(allowedAreaHelp_, nullptr, TRUE);
    const bool local = movementMode_ && SendMessageW(movementMode_, CB_GETCURSEL, 0, 0) == 3;
    if (localAreaRadius_) EnableWindow(localAreaRadius_, local ? TRUE : FALSE);
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
    next.fontSizeDip = readDouble(fontSize_, next.fontSizeDip);
    next.opacity = readDouble(opacity_, next.opacity * 100.0) / 100.0;
    next.boostOpacity = readDouble(boostOpacity_, next.boostOpacity * 100.0) / 100.0;
    const int durationSelection = static_cast<int>(SendMessageW(boostDuration_, CB_GETCURSEL, 0, 0));
    constexpr int durations[] = {5, 10, 15, 30, 60, 120};
    if (durationSelection >= 0 && durationSelection < static_cast<int>(std::size(durations))) {
        next.boostDurationSeconds = durations[durationSelection];
    }
}

void SettingsWindow::readMovementPage(core::Settings& next) const {
    const int movement = static_cast<int>(SendMessageW(movementMode_, CB_GETCURSEL, 0, 0));
    next.movementMode = movement == 0 ? core::MovementMode::EdgeOnly :
                        movement == 1 ? core::MovementMode::FourCorners :
                        movement == 2 ? core::MovementMode::WholeScreen : core::MovementMode::LocalWander;
    next.localAreaRadiusPx = readBoundedInteger(localAreaRadius_, next.localAreaRadiusPx, 1, 10000);
    const int currentHours = next.movementIntervalSeconds / 3600;
    const int currentMinutes = (next.movementIntervalSeconds / 60) % 60;
    const int currentSeconds = next.movementIntervalSeconds % 60;
    constexpr int maxHours = std::numeric_limits<int>::max() / 3600;
    const int hours = readBoundedInteger(intervalHours_, currentHours, 0, maxHours);
    const int minutes = readBoundedInteger(intervalMinutes_, currentMinutes, 0, 59);
    const int seconds = readBoundedInteger(intervalSeconds_, currentSeconds, 0, 59);
    const long long totalSeconds = static_cast<long long>(hours) * 3600LL + minutes * 60LL + seconds;
    next.movementIntervalSeconds = static_cast<int>(std::clamp<long long>(
        totalSeconds, 1, std::numeric_limits<int>::max()));
    next.microShiftEnabled = SendMessageW(microShiftEnabled_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    next.microShiftCount = readBoundedInteger(microShiftCount_, next.microShiftCount, 1, 100);
    next.microShiftDistancePx = readBoundedInteger(microShiftDistance_, next.microShiftDistancePx, 1, 100);
    next.edgeMarginDip = readDouble(edgeMargin_, next.edgeMarginDip);
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
    syncing_ = true;
    const int hours = settings_.movementIntervalSeconds / 3600;
    const int minutes = (settings_.movementIntervalSeconds / 60) % 60;
    const int seconds = settings_.movementIntervalSeconds % 60;
    setText(intervalHours_, std::to_wstring(hours));
    setText(intervalMinutes_, std::to_wstring(minutes));
    setText(intervalSeconds_, std::to_wstring(seconds));
    setText(localAreaRadius_, std::to_wstring(settings_.localAreaRadiusPx));
    setText(microShiftCount_, std::to_wstring(settings_.microShiftCount));
    setText(microShiftDistance_, std::to_wstring(settings_.microShiftDistancePx));
    EnableWindow(microShiftCount_, settings_.microShiftEnabled ? TRUE : FALSE);
    EnableWindow(microShiftDistance_, settings_.microShiftEnabled ? TRUE : FALSE);
    setText(allowedLeft_, numberText(settings_.allowedArea.left * 100.0));
    setText(allowedTop_, numberText(settings_.allowedArea.top * 100.0));
    setText(allowedRight_, numberText(settings_.allowedArea.right * 100.0));
    setText(allowedBottom_, numberText(settings_.allowedArea.bottom * 100.0));
    updateMovementEditorState();
    syncing_ = false;
    dirty_ = true;
    setText(statusLabel_, committed ? L"Changes applied." : L"Unsaved changes — select Apply when ready.");
    if (committed && onApply_) {
        onApply_(settings_);
        dirty_ = false;
        clearPendingEdits();
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
        setText(colorButton_, L"Clock color: " + std::to_wstring(settings_.textColor.r) + L", " +
                               std::to_wstring(settings_.textColor.g) + L", " + std::to_wstring(settings_.textColor.b));
        dirty_ = true;
        setText(statusLabel_, L"Unsaved changes — select Apply when ready.");
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
    case WM_ERASEBKGND: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        RECT client{};
        GetClientRect(hwnd_, &client);
        FillRect(dc, &client, stockBrush(dc, kShellColor));
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd_, &paint);
        if (footerTop_ > 0) {
            RECT client{};
            GetClientRect(hwnd_, &client);
            RECT divider{scale(24), footerTop_, std::max<LONG>(scale(24), client.right - scale(24)),
                         footerTop_ + std::max(1, scale(1))};
            FillRect(dc, &divider, stockBrush(dc, kBorderColor));
        }
        EndPaint(hwnd_, &paint);
        return 0;
    }
    case WM_DRAWITEM: {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (!item) break;
        if (item->CtlID == IdTabs) {
            drawTab(*item);
            return TRUE;
        }
        if (item->CtlType == ODT_BUTTON) {
            drawButton(*item);
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        HWND control = reinterpret_cast<HWND>(lParam);
        const bool shellControl = control == titleLabel_ || control == subtitleLabel_ ||
                                  control == statusLabel_;
        const bool customAreaSelected = allowedPreset_ &&
            SendMessageW(allowedPreset_, CB_GETCURSEL, 0, 0) == 3;
        const bool muted = control == subtitleLabel_ || control == statusLabel_ ||
                           (control == allowedAreaHelp_ && !customAreaSelected) ||
                           control == localAreaHelp_ || control == displayHelp_ ||
                           control == fontSizeValue_ || control == opacityValue_ ||
                           control == boostOpacityValue_;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, muted ? kMutedTextColor : kTextColor);
        return reinterpret_cast<LRESULT>(stockBrush(dc, shellControl ? kShellColor : kSurfaceColor));
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        const HWND control = reinterpret_cast<HWND>(lParam);
        const bool pendingEdit = message == WM_CTLCOLOREDIT && pendingEdits_.contains(control);
        const COLORREF background = pendingEdit ? kAccentSoftColor : kSurfaceColor;
        SetBkColor(dc, background);
        SetTextColor(dc, kTextColor);
        return reinterpret_cast<LRESULT>(stockBrush(dc, background));
    }
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
        UniqueGdiFont nextTitleFont(CreateFontW(-scale(24), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                                L"Segoe UI Variable Display"));
        if (nextFont) {
            for (HWND control : allControls_) setControlFont(control, nextFont.get());
            // Controls now reference the replacement; moving it into the owner
            // releases the previous font only after the handoff is complete.
            controlFont_ = std::move(nextFont);
        }
        if (nextTitleFont) {
            setControlFont(titleLabel_, nextTitleFont.get());
            titleFont_ = std::move(nextTitleFont);
        }
        applyVisualTheme();
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
            else if (id == IdReset) {
                settings_ = core::Settings::defaults();
                syncToControls();
                dirty_ = true;
                setText(statusLabel_, L"Defaults ready — select Apply to save them.");
            }
            else if (id == IdPreset) {
                settings_ = core::Settings::oledPreset();
                syncToControls();
                dirty_ = true;
                setText(statusLabel_, L"OLED preset ready — select Apply to save it.");
            }
            else if (id == IdPosition && onPositioning_) onPositioning_();
            else if (id == IdStatistics && onStatistics_) onStatistics_();
            else if (id == IdClose) applyFromControls(true);
            else applyFromControls(false);
        } else if (notification == EN_CHANGE) {
            markPendingEdit(reinterpret_cast<HWND>(lParam));
        } else if (notification == CBN_SELCHANGE) {
            if (id == IdAllowedPreset || id == IdMovementMode) updateMovementEditorState();
            applyFromControls(false);
        } else if (notification == EN_KILLFOCUS) {
            applyFromControls(false);
        }
        return 0;
    }
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
        hide();
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

} // namespace aoc::platform

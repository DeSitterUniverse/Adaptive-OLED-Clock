#include "aoc/platform/settings_window.h"

#include "aoc/core/geometry.h"

#include <commctrl.h>
#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace aoc::platform {
namespace {

constexpr wchar_t kSettingsClass[] = L"AdaptiveOledClockSettingsWindow";
enum ControlId {
    IdTimeFormat = 100,
    IdShowAmPm,
    IdFontSize,
    IdColor,
    IdOpacity,
    IdInterval,
    IdMovementMode,
    IdAllowedLeft,
    IdAllowedTop,
    IdAllowedRight,
    IdAllowedBottom,
    IdExcludedLeft,
    IdExcludedTop,
    IdExcludedRight,
    IdExcludedBottom,
    IdExcludedList,
    IdExcludedAdd,
    IdExcludedRemove,
    IdExcludedClear,
    IdEdgeMargin,
    IdMonitorMode,
    IdFixedMonitor,
    IdPreferredX,
    IdPreferredY,
    IdFullscreen,
    IdStartup,
    IdHotkey,
    IdBoostOpacity,
    IdBoostDuration,
    IdReset,
    IdPreset,
    IdPosition,
    IdClose,
};

void setText(HWND control, const std::wstring& value) {
    if (control) SetWindowTextW(control, value.c_str());
}

std::wstring numberText(double value) {
    std::wostringstream stream;
    stream << std::fixed << std::setprecision(3) << value;
    return stream.str();
}

double readDouble(HWND control, double fallback) {
    wchar_t buffer[128]{};
    if (!control || GetWindowTextW(control, buffer, static_cast<int>(std::size(buffer))) == 0) return fallback;
    wchar_t* end = nullptr;
    const double value = wcstod(buffer, &end);
    return end != buffer && std::isfinite(value) ? value : fallback;
}

std::wstring fromUtf8(const std::string& value) {
    if (value.empty()) return {};
    const int required = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) return {};
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), required);
    return result;
}

std::string toUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int required = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), required, nullptr, nullptr);
    return result;
}

} // namespace

SettingsWindow::~SettingsWindow() {
    if (hwnd_) DestroyWindow(hwnd_);
}

bool SettingsWindow::create(HINSTANCE instance, HWND owner, ApplyCallback onApply,
                            SimpleCallback onReset, SimpleCallback onPreset,
                            SimpleCallback onPositioning) {
    if (hwnd_) return true;
    instance_ = instance;
    owner_ = owner;
    onApply_ = std::move(onApply);
    onReset_ = std::move(onReset);
    onPreset_ = std::move(onPreset);
    onPositioning_ = std::move(onPositioning);
    WNDCLASSEXW windowClass{sizeof(WNDCLASSEXW)};
    windowClass.hInstance = instance_;
    windowClass.lpfnWndProc = &SettingsWindow::windowProc;
    windowClass.lpszClassName = kSettingsClass;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassExW(&windowClass);
    hwnd_ = CreateWindowExW(WS_EX_DLGMODALFRAME, kSettingsClass, L"Adaptive OLED Clock Settings",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT, 650, 930, owner_, nullptr, instance_, this);
    return hwnd_ != nullptr;
}

void SettingsWindow::show(const core::Settings& settings, const std::vector<core::MonitorInfo>& monitors) {
    if (!hwnd_) return;
    settings_ = settings;
    monitors_ = monitors;
    syncToControls();
    RECT ownerRect{};
    if (owner_ && GetWindowRect(owner_, &ownerRect)) {
        SetWindowPos(hwnd_, HWND_TOP, ownerRect.left + 32, ownerRect.top + 32, 0, 0,
                     SWP_NOSIZE | SWP_SHOWWINDOW);
    } else {
        ShowWindow(hwnd_, SW_SHOWNORMAL);
    }
    SetForegroundWindow(hwnd_);
}

void SettingsWindow::hide() {
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

HWND SettingsWindow::addControl(DWORD style, const wchar_t* className, const wchar_t* text,
                                int id, int x, int y, int width, int height, DWORD exStyle) {
    HWND control = CreateWindowExW(exStyle, className, text, WS_CHILD | WS_VISIBLE | style,
                                  x, y, width, height, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                  instance_, nullptr);
    if (control) controls_.push_back(control);
    return control;
}

void SettingsWindow::addLabel(const wchar_t* text, int x, int y, int width, int height) {
    HWND label = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                                x, y, width, height, hwnd_, nullptr, instance_, nullptr);
    if (label) labels_.push_back(label);
}

void SettingsWindow::createControls() {
    int y = 16;
    constexpr int labelX = 16;
    constexpr int fieldX = 286;
    constexpr int fieldWidth = 300;
    auto row = [&](const wchar_t* label, int id, const wchar_t* className, DWORD style, const wchar_t* text = L"") {
        addLabel(label, labelX, y + 4, 255, 22);
        return addControl(style, className, text, id, fieldX, y, fieldWidth, 24);
    };
    timeFormat_ = row(L"Time format", IdTimeFormat, L"COMBOBOX", CBS_DROPDOWNLIST | WS_VSCROLL);
    SendMessageW(timeFormat_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Locale-derived"));
    SendMessageW(timeFormat_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"12-hour"));
    SendMessageW(timeFormat_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"24-hour"));
    y += 32;
    addLabel(L"Show AM/PM in 12-hour mode", labelX, y + 4, 255, 22);
    showAmPm_ = addControl(BS_AUTOCHECKBOX, L"BUTTON", L"Enabled", IdShowAmPm, fieldX, y, fieldWidth, 24);
    y += 32;
    fontSize_ = row(L"Font size (DIP)", IdFontSize, L"EDIT", ES_AUTOHSCROLL | WS_BORDER, L"32"); y += 32;
    addLabel(L"Text color", labelX, y + 4, 255, 22);
    colorButton_ = addControl(BS_PUSHBUTTON, L"BUTTON", L"Choose color...", IdColor, fieldX, y, fieldWidth, 24); y += 32;
    opacity_ = row(L"Normal opacity (0-1)", IdOpacity, L"EDIT", ES_AUTOHSCROLL | WS_BORDER, L"0.32"); y += 32;
    interval_ = row(L"Major movement interval (minutes)", IdInterval, L"EDIT", ES_AUTOHSCROLL | WS_BORDER, L"5"); y += 32;
    movementMode_ = row(L"Movement mode", IdMovementMode, L"COMBOBOX", CBS_DROPDOWNLIST | WS_VSCROLL);
    SendMessageW(movementMode_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Whole screen"));
    SendMessageW(movementMode_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Local wander"));
    y += 38;

    addLabel(L"Allowed movement area (normalized 0-1)", labelX, y + 4, 255, 22);
    allowedLeft_ = addControl(ES_AUTOHSCROLL | WS_BORDER, L"EDIT", L"0", IdAllowedLeft, fieldX, y, 66, 24);
    allowedTop_ = addControl(ES_AUTOHSCROLL | WS_BORDER, L"EDIT", L"0", IdAllowedTop, fieldX + 76, y, 66, 24);
    allowedRight_ = addControl(ES_AUTOHSCROLL | WS_BORDER, L"EDIT", L"1", IdAllowedRight, fieldX + 152, y, 66, 24);
    allowedBottom_ = addControl(ES_AUTOHSCROLL | WS_BORDER, L"EDIT", L"1", IdAllowedBottom, fieldX + 228, y, 66, 24);
    y += 32;
    addLabel(L"Excluded area editor (normalized 0-1)", labelX, y + 4, 255, 22);
    excludedLeft_ = addControl(ES_AUTOHSCROLL | WS_BORDER, L"EDIT", L"0", IdExcludedLeft, fieldX, y, 66, 24);
    excludedTop_ = addControl(ES_AUTOHSCROLL | WS_BORDER, L"EDIT", L"0", IdExcludedTop, fieldX + 76, y, 66, 24);
    excludedRight_ = addControl(ES_AUTOHSCROLL | WS_BORDER, L"EDIT", L"0", IdExcludedRight, fieldX + 152, y, 66, 24);
    excludedBottom_ = addControl(ES_AUTOHSCROLL | WS_BORDER, L"EDIT", L"0", IdExcludedBottom, fieldX + 228, y, 66, 24);
    y += 30;
    excludedList_ = addControl(LBS_NOTIFY | WS_BORDER | WS_VSCROLL, L"LISTBOX", L"", IdExcludedList,
                               fieldX, y, fieldWidth, 84);
    y += 92;
    addControl(BS_PUSHBUTTON, L"BUTTON", L"Add exclusion", IdExcludedAdd, fieldX, y, 96, 24);
    addControl(BS_PUSHBUTTON, L"BUTTON", L"Remove selected", IdExcludedRemove, fieldX + 104, y, 112, 24);
    addControl(BS_PUSHBUTTON, L"BUTTON", L"Clear all", IdExcludedClear, fieldX + 224, y, 76, 24);
    y += 34;
    edgeMargin_ = row(L"Edge margin (DIP)", IdEdgeMargin, L"EDIT", ES_AUTOHSCROLL | WS_BORDER, L"24"); y += 32;
    monitorMode_ = row(L"Monitor selection", IdMonitorMode, L"COMBOBOX", CBS_DROPDOWNLIST | WS_VSCROLL);
    SendMessageW(monitorMode_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Follow primary monitor"));
    y += 32;
    fixedMonitor_ = row(L"Fixed monitor key", IdFixedMonitor, L"EDIT", ES_AUTOHSCROLL | WS_BORDER); y += 32;
    addLabel(L"Preferred initial position (normalized)", labelX, y + 4, 255, 22);
    preferredX_ = addControl(ES_AUTOHSCROLL | WS_BORDER, L"EDIT", L"0.5", IdPreferredX, fieldX, y, 142, 24);
    preferredY_ = addControl(ES_AUTOHSCROLL | WS_BORDER, L"EDIT", L"0.5", IdPreferredY, fieldX + 158, y, 142, 24);
    y += 34;
    addLabel(L"Hide during fullscreen apps", labelX, y + 4, 255, 22);
    fullscreen_ = addControl(BS_AUTOCHECKBOX, L"BUTTON", L"Enabled", IdFullscreen, fieldX, y, fieldWidth, 24); y += 30;
    addLabel(L"Launch at Windows sign-in", labelX, y + 4, 255, 22);
    startup_ = addControl(BS_AUTOCHECKBOX, L"BUTTON", L"Enabled", IdStartup, fieldX, y, fieldWidth, 24); y += 30;
    addLabel(L"Global Ctrl+Alt+C hotkey", labelX, y + 4, 255, 22);
    hotkey_ = addControl(BS_AUTOCHECKBOX, L"BUTTON", L"Enabled", IdHotkey, fieldX, y, fieldWidth, 24); y += 32;
    boostOpacity_ = row(L"Temporary brightness opacity", IdBoostOpacity, L"EDIT", ES_AUTOHSCROLL | WS_BORDER, L"0.85"); y += 32;
    boostDuration_ = row(L"Temporary brightness duration (seconds)", IdBoostDuration, L"EDIT", ES_AUTOHSCROLL | WS_BORDER, L"15"); y += 42;
    addControl(BS_DEFPUSHBUTTON, L"BUTTON", L"Reset to defaults", IdReset, labelX, y, 124, 28);
    addControl(BS_PUSHBUTTON, L"BUTTON", L"OLED-safe preset", IdPreset, labelX + 134, y, 124, 28);
    addControl(BS_PUSHBUTTON, L"BUTTON", L"Drag to position", IdPosition, labelX + 268, y, 124, 28);
    addControl(BS_PUSHBUTTON, L"BUTTON", L"Close", IdClose, fieldX + 176, y, 124, 28);
}

void SettingsWindow::layoutControls(int, int) {
    // The initial layout is intentionally fixed and fully visible at the minimum window size.
}

void SettingsWindow::syncToControls() {
    if (!hwnd_) return;
    syncing_ = true;
    SendMessageW(timeFormat_, CB_SETCURSEL, static_cast<int>(settings_.timeFormat), 0);
    SendMessageW(movementMode_, CB_SETCURSEL, static_cast<int>(settings_.movementMode), 0);
    SendMessageW(monitorMode_, CB_RESETCONTENT, 0, 0);
    SendMessageW(monitorMode_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Follow primary monitor"));
    int monitorSelection = 0;
    for (std::size_t index = 0; index < monitors_.size(); ++index) {
        const std::wstring key = fromUtf8(monitors_[index].stableKey);
        const std::wstring label = L"Fixed: " + key;
        SendMessageW(monitorMode_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        if (settings_.monitorMode == core::MonitorMode::Fixed && monitors_[index].stableKey == settings_.fixedMonitorKey) {
            monitorSelection = static_cast<int>(index + 1);
        }
    }
    SendMessageW(monitorMode_, CB_SETCURSEL, monitorSelection, 0);
    SendMessageW(showAmPm_, BM_SETCHECK, settings_.showAmPm ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(fullscreen_, BM_SETCHECK, settings_.hideInFullscreen ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(startup_, BM_SETCHECK, settings_.launchAtStartup ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(hotkey_, BM_SETCHECK, settings_.hotkeyEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    setText(fontSize_, numberText(settings_.fontSizeDip));
    setText(opacity_, numberText(settings_.opacity));
    setText(interval_, std::to_wstring(settings_.movementIntervalMinutes));
    setText(allowedLeft_, numberText(settings_.allowedArea.left));
    setText(allowedTop_, numberText(settings_.allowedArea.top));
    setText(allowedRight_, numberText(settings_.allowedArea.right));
    setText(allowedBottom_, numberText(settings_.allowedArea.bottom));
    setText(edgeMargin_, numberText(settings_.edgeMarginDip));
    setText(fixedMonitor_, fromUtf8(settings_.fixedMonitorKey));
    setText(preferredX_, numberText(settings_.preferredPosition.x));
    setText(preferredY_, numberText(settings_.preferredPosition.y));
    setText(boostOpacity_, numberText(settings_.boostOpacity));
    setText(boostDuration_, std::to_wstring(settings_.boostDurationSeconds));
    updateExcludedList();
    syncing_ = false;
}

void SettingsWindow::applyFromControls() {
    if (syncing_) return;
    core::Settings next = settings_;
    const int format = static_cast<int>(SendMessageW(timeFormat_, CB_GETCURSEL, 0, 0));
    const int movement = static_cast<int>(SendMessageW(movementMode_, CB_GETCURSEL, 0, 0));
    next.timeFormat = format < 0 ? core::TimeFormat::Locale : static_cast<core::TimeFormat>(format);
    next.movementMode = movement < 0 ? core::MovementMode::WholeScreen : static_cast<core::MovementMode>(movement);
    const int monitorSelection = static_cast<int>(SendMessageW(monitorMode_, CB_GETCURSEL, 0, 0));
    next.monitorMode = monitorSelection > 0 ? core::MonitorMode::Fixed : core::MonitorMode::FollowPrimary;
    next.showAmPm = SendMessageW(showAmPm_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    next.hideInFullscreen = SendMessageW(fullscreen_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    next.launchAtStartup = SendMessageW(startup_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    next.hotkeyEnabled = SendMessageW(hotkey_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    next.fontSizeDip = readDouble(fontSize_, next.fontSizeDip);
    next.opacity = readDouble(opacity_, next.opacity);
    next.movementIntervalMinutes = std::clamp(static_cast<int>(readDouble(interval_, next.movementIntervalMinutes)), 1, 120);
    next.allowedArea = {readDouble(allowedLeft_, next.allowedArea.left), readDouble(allowedTop_, next.allowedArea.top),
                        readDouble(allowedRight_, next.allowedArea.right), readDouble(allowedBottom_, next.allowedArea.bottom)};
    next.edgeMarginDip = readDouble(edgeMargin_, next.edgeMarginDip);
    const std::wstring fixed = [&] { wchar_t value[512]{}; GetWindowTextW(fixedMonitor_, value, 512); return std::wstring(value); }();
    next.fixedMonitorKey = toUtf8(fixed);
    if (next.monitorMode == core::MonitorMode::Fixed && next.fixedMonitorKey.empty() &&
        static_cast<std::size_t>(monitorSelection - 1) < monitors_.size()) {
        next.fixedMonitorKey = monitors_[monitorSelection - 1].stableKey;
    }
    next.preferredPosition = {readDouble(preferredX_, next.preferredPosition.x), readDouble(preferredY_, next.preferredPosition.y)};
    next.boostOpacity = readDouble(boostOpacity_, next.boostOpacity);
    next.boostDurationSeconds = std::clamp(static_cast<int>(readDouble(boostDuration_, next.boostDurationSeconds)), 1, 300);
    next.validateAndNormalize();
    settings_ = next;
    if (onApply_) onApply_(settings_);
}

void SettingsWindow::addExcludedArea() {
    const core::NormalizedRect rect{readDouble(excludedLeft_, 0.0), readDouble(excludedTop_, 0.0),
                                    readDouble(excludedRight_, 0.0), readDouble(excludedBottom_, 0.0)};
    if (rect.isValid()) {
        settings_.excludedAreas.push_back(core::clampNormalizedRect(rect));
        settings_.validateAndNormalize();
        updateExcludedList();
        if (onApply_) onApply_(settings_);
    }
}

void SettingsWindow::removeSelectedExcludedArea() {
    const LRESULT selected = SendMessageW(excludedList_, LB_GETCURSEL, 0, 0);
    if (selected >= 0 && static_cast<std::size_t>(selected) < settings_.excludedAreas.size()) {
        settings_.excludedAreas.erase(settings_.excludedAreas.begin() + selected);
        updateExcludedList();
        if (onApply_) onApply_(settings_);
    }
}

void SettingsWindow::clearExcludedAreas() {
    settings_.excludedAreas.clear();
    updateExcludedList();
    if (onApply_) onApply_(settings_);
}

void SettingsWindow::updateExcludedList() {
    if (!excludedList_) return;
    SendMessageW(excludedList_, LB_RESETCONTENT, 0, 0);
    for (const auto& rect : settings_.excludedAreas) {
        std::wostringstream item;
        item << std::fixed << std::setprecision(3) << rect.left << L", " << rect.top << L" - "
             << rect.right << L", " << rect.bottom;
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
        if (onApply_) onApply_(settings_);
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
    switch (message) {
    case WM_CREATE:
        createControls();
        return 0;
    case WM_SIZE:
        layoutControls(LOWORD(lParam), HIWORD(lParam));
        return 0;
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
            else if (id == IdClose) hide();
            else applyFromControls();
        } else if (notification == CBN_SELCHANGE || notification == EN_CHANGE) {
            applyFromControls();
        }
        return 0;
    }
    case WM_HSCROLL:
        applyFromControls();
        return 0;
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

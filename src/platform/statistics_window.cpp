#include "aoc/platform/statistics_window.h"
#include "aoc/platform/win32_ui.h"

#include <commctrl.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace aoc::platform {
namespace {

constexpr wchar_t kStatisticsClass[] = L"AdaptiveOledClockStatisticsWindow";
constexpr int kMonitorId = 6100;
constexpr int kResetId = 6101;
constexpr int kExportId = 6102;
constexpr int kCloseId = 6103;

const wchar_t* modeName(core::MovementMode mode) {
    switch (mode) {
    case core::MovementMode::WholeScreen: return L"Whole screen";
    case core::MovementMode::LocalWander: return L"Local area";
    case core::MovementMode::EdgeOnly: return L"Edge only";
    case core::MovementMode::FourCorners: return L"Four corners";
    default: return L"Unknown";
    }
}

} // namespace

StatisticsWindow::~StatisticsWindow() {
    if (hwnd_) DestroyWindow(hwnd_);
}

bool StatisticsWindow::create(HINSTANCE instance, HWND owner, SimpleCallback onReset,
                              SimpleCallback onExport) {
    if (hwnd_) return true;
    instance_ = instance;
    owner_ = owner;
    onReset_ = std::move(onReset);
    onExport_ = std::move(onExport);
    WNDCLASSEXW windowClass{sizeof(WNDCLASSEXW)};
    windowClass.hInstance = instance_;
    windowClass.lpfnWndProc = &StatisticsWindow::windowProc;
    windowClass.lpszClassName = kStatisticsClass;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = loadApplicationIcon(instance_, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    windowClass.hIconSm = loadApplicationIcon(instance_, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    // The controller is HWND_MESSAGE-only. Statistics is deliberately a regular
    // top-level window so it remains discoverable and keyboard-accessible.
    hwnd_ = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, kStatisticsClass, L"Adaptive OLED Clock - Movement History",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT, 720, 600, nullptr, nullptr, instance_, this);
    if (!hwnd_) return false;
    dpi_ = GetDpiForWindow(hwnd_);
    if (dpi_ == 0) dpi_ = 96;
    controlFont_.reset(createControlFont(dpi_));
    if (!controlFont_) return false;
    monitorCombo_ = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                    0, 0, scale(320), scale(240), hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMonitorId)), instance_, nullptr);
    totalLabel_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 400, 24, hwnd_, nullptr, instance_, nullptr);
    leastLabel_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 400, 24, hwnd_, nullptr, instance_, nullptr);
    imbalanceLabel_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 400, 24, hwnd_, nullptr, instance_, nullptr);
    modeLabel_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 400, 24, hwnd_, nullptr, instance_, nullptr);
    resetButton_ = CreateWindowExW(0, L"BUTTON", L"Clear history...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   0, 0, 170, 30, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kResetId)), instance_, nullptr);
    exportButton_ = CreateWindowExW(0, L"BUTTON", L"Save CSV...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                    0, 0, 120, 30, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kExportId)), instance_, nullptr);
    closeButton_ = CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                   0, 0, 90, 30, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCloseId)), instance_, nullptr);
    controls_ = {monitorCombo_, totalLabel_, leastLabel_, imbalanceLabel_, modeLabel_,
                 resetButton_, exportButton_, closeButton_};
    if (std::any_of(controls_.begin(), controls_.end(), [](HWND control) { return !control; })) return false;
    for (HWND control : controls_) setControlFont(control, controlFont_.get());
    return true;
}

int StatisticsWindow::scale(int value) const noexcept {
    return scaleDip(value, dpi_);
}

void StatisticsWindow::show(const std::vector<core::MonitorInfo>& monitors,
                            const core::ExposureStore& exposure,
                            const std::string& selectedKey,
                            core::MovementMode mode) {
    if (!hwnd_) return;
    refresh(monitors, exposure, selectedKey, mode);
    const RECT bounds = centeredWindowRect(owner_, hwnd_, 640, 520, dpi_);
    SetWindowPos(hwnd_, HWND_TOP, bounds.left, bounds.top,
                 bounds.right - bounds.left, bounds.bottom - bounds.top, SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd_);
}

void StatisticsWindow::refresh(const std::vector<core::MonitorInfo>& monitors,
                               const core::ExposureStore& exposure,
                               const std::string& selectedKey,
                               core::MovementMode mode) {
    if (!hwnd_) return;
    monitors_ = monitors;
    exposure_ = exposure;
    mode_ = mode;
    const bool requestedMonitorExists = !selectedKey.empty() && std::any_of(monitors_.begin(), monitors_.end(), [&](const auto& monitor) {
        return monitor.stableKey == selectedKey;
    });
    const bool currentMonitorExists = !selectedKey_.empty() && std::any_of(monitors_.begin(), monitors_.end(), [&](const auto& monitor) {
        return monitor.stableKey == selectedKey_;
    });
    if (requestedMonitorExists) {
        selectedKey_ = selectedKey;
    } else if ((!currentMonitorExists || selectedKey_.empty()) && !monitors_.empty()) {
        selectedKey_ = monitors_.front().stableKey;
    } else if (monitors_.empty()) {
        selectedKey_.clear();
    }
    syncMonitorCombo();
    updateSummary();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void StatisticsWindow::hide() {
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

void StatisticsWindow::syncMonitorCombo() {
    if (!monitorCombo_) return;
    SendMessageW(monitorCombo_, CB_RESETCONTENT, 0, 0);
    int selected = 0;
    for (std::size_t index = 0; index < monitors_.size(); ++index) {
        const auto& monitor = monitors_[index];
        std::wstring name = wideFromUtf8(monitor.stableKey);
        if (!monitor.displayName.empty()) name = monitor.displayName;
        if (monitor.primary) name += L" (Primary)";
        SendMessageW(monitorCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        if (monitor.stableKey == selectedKey_) selected = static_cast<int>(index);
    }
    if (!monitors_.empty()) SendMessageW(monitorCombo_, CB_SETCURSEL, selected, 0);
    SendMessageW(monitorCombo_, CB_SETMINVISIBLE, 8, 0);
    SendMessageW(monitorCombo_, CB_SETDROPPEDWIDTH, scale(360), 0);
}

void StatisticsWindow::updateSummary() {
    const core::ExposureMap* map = exposure_.find(selectedKey_);
    const core::ExposureMap empty;
    if (!map) map = &empty;
    const auto least = map->leastExposedCell();
    const auto most = map->mostExposedCell();
    const double trackedMinutes = map->totalSeconds() / 60.0;
    std::wostringstream total;
    total << L"Time tracked: " << std::fixed << std::setprecision(1) << trackedMinutes << L" minutes";
    std::wostringstream leastText;
    std::wostringstream imbalance;
    if (trackedMinutes < 5.0) {
        leastText << L"Least-used area: available after 5 minutes";
        imbalance << L"Coverage: collecting more history";
    } else {
        leastText << L"Least-used area: column " << least.first + 1 << L", row " << least.second + 1;
        imbalance << L"Coverage difference: " << std::fixed << std::setprecision(1) << map->imbalance() * 100.0
                  << L"% (most-used area: column " << most.first + 1 << L", row " << most.second + 1 << L")";
    }
    SetWindowTextW(totalLabel_, total.str().c_str());
    SetWindowTextW(leastLabel_, leastText.str().c_str());
    SetWindowTextW(imbalanceLabel_, imbalance.str().c_str());
    std::wstring modeText = L"Movement: ";
    modeText += modeName(mode_);
    SetWindowTextW(modeLabel_, modeText.c_str());
}

void StatisticsWindow::layoutControls(int width, int height) {
    width = std::max(1, width);
    height = std::max(1, height);
    const int margin = std::min(scale(18), std::max(0, (width - 1) / 2));
    const int contentWidth = std::max(1, width - 2 * margin);
    const int buttonGap = scale(8);
    const int buttonHeight = scale(30);
    const int buttonAvailableWidth = contentWidth;
    const int buttonNaturalWidths[] = {175, 120, 90};
    int buttonRows = 1;
    int buttonCursor = 0;
    for (const int naturalWidth : buttonNaturalWidths) {
        const int buttonWidth = std::min(scale(naturalWidth), buttonAvailableWidth);
        if (buttonCursor > 0 && buttonCursor + buttonWidth > buttonAvailableWidth) {
            ++buttonRows;
            buttonCursor = 0;
        }
        buttonCursor += buttonWidth + buttonGap;
    }
    const int buttonAreaTop = height - margin - buttonRows * buttonHeight -
                              (buttonRows - 1) * buttonGap;
    int y = margin;
    if (monitorCombo_) MoveWindow(monitorCombo_, margin, y, contentWidth, scale(28), TRUE);
    y += scale(42);
    for (HWND label : {totalLabel_, leastLabel_, imbalanceLabel_, modeLabel_}) {
        if (label) MoveWindow(label, margin, y, contentWidth, scale(24), TRUE);
        y += scale(26);
    }
    const int heatmapTop = std::min(std::max(0, y + scale(8)), height);
    const int heatmapBottom = std::clamp(std::max(heatmapTop + 1, buttonAreaTop - buttonGap),
                                         heatmapTop + 1, std::max(heatmapTop + 1, height));
    heatmapRect_ = {margin, heatmapTop, margin + contentWidth, heatmapBottom};

    int buttonX = margin;
    int buttonY = buttonAreaTop;
    auto placeButton = [&](HWND button, int naturalWidth) {
        const int buttonWidth = std::min(scale(naturalWidth), buttonAvailableWidth);
        if (buttonX != margin && buttonX + buttonWidth > width - margin) {
            buttonX = margin;
            buttonY += buttonHeight + buttonGap;
        }
        if (button) MoveWindow(button, buttonX, buttonY, buttonWidth, buttonHeight, TRUE);
        buttonX += buttonWidth + buttonGap;
    };
    placeButton(resetButton_, 175);
    placeButton(exportButton_, 120);
    placeButton(closeButton_, 90);
}

void StatisticsWindow::paintHeatmap(HDC dc, const RECT&) {
    HBRUSH background = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
    FillRect(dc, &heatmapRect_, background);
    DeleteObject(background);
    const core::ExposureMap* map = exposure_.find(selectedKey_);
    const core::ExposureMap empty;
    if (!map) map = &empty;
    double maximum = 0.0;
    for (double value : map->seconds) maximum = std::max(maximum, value);
    const int axisWidth = scale(28);
    const int axisHeight = scale(20);
    const RECT grid{heatmapRect_.left + axisWidth, heatmapRect_.top + axisHeight,
                    heatmapRect_.right, heatmapRect_.bottom};
    const int heatmapWidth = std::max(1, static_cast<int>(grid.right - grid.left));
    const int heatmapHeight = std::max(1, static_cast<int>(grid.bottom - grid.top));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    const HGDIOBJ previousFont = controlFont_ ? SelectObject(dc, controlFont_.get()) : nullptr;
    for (std::size_t column = 0; column < core::kExposureColumns; ++column) {
        RECT label{grid.left + static_cast<int>(column) * heatmapWidth / static_cast<int>(core::kExposureColumns),
                   heatmapRect_.top,
                   grid.left + static_cast<int>(column + 1) * heatmapWidth / static_cast<int>(core::kExposureColumns),
                   grid.top};
        const std::wstring text = std::to_wstring(column + 1);
        DrawTextW(dc, text.c_str(), -1, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    for (std::size_t row = 0; row < core::kExposureRows; ++row) {
        RECT label{heatmapRect_.left,
                   grid.top + static_cast<int>(row) * heatmapHeight / static_cast<int>(core::kExposureRows),
                   grid.left - scale(5),
                   grid.top + static_cast<int>(row + 1) * heatmapHeight / static_cast<int>(core::kExposureRows)};
        const std::wstring text = std::to_wstring(row + 1);
        DrawTextW(dc, text.c_str(), -1, &label, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    for (std::size_t row = 0; row < core::kExposureRows; ++row) {
        for (std::size_t column = 0; column < core::kExposureColumns; ++column) {
            const double intensity = maximum <= 0.0 ? 0.0 : map->cell(column, row) / maximum;
            const BYTE red = static_cast<BYTE>(std::clamp(38.0 + intensity * 180.0, 0.0, 255.0));
            const BYTE green = static_cast<BYTE>(std::clamp(170.0 - intensity * 125.0, 0.0, 255.0));
            const BYTE blue = static_cast<BYTE>(std::clamp(225.0 - intensity * 155.0, 0.0, 255.0));
            const COLORREF color = RGB(red, green, blue);
            RECT cell{grid.left + static_cast<int>(column) * heatmapWidth / static_cast<int>(core::kExposureColumns),
                      grid.top + static_cast<int>(row) * heatmapHeight / static_cast<int>(core::kExposureRows),
                      grid.left + static_cast<int>(column + 1) * heatmapWidth / static_cast<int>(core::kExposureColumns),
                      grid.top + static_cast<int>(row + 1) * heatmapHeight / static_cast<int>(core::kExposureRows)};
            HBRUSH brush = CreateSolidBrush(color);
            FillRect(dc, &cell, brush);
            DeleteObject(brush);
            FrameRect(dc, &cell, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        }
    }
    if (previousFont) SelectObject(dc, previousFont);
}

LRESULT CALLBACK StatisticsWindow::windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<StatisticsWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<StatisticsWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = window;
    }
    return self ? self->handleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT StatisticsWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
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
        if (dpi_ == 0) dpi_ = 96;
        UniqueGdiFont nextFont(createControlFont(dpi_));
        if (nextFont) {
            for (HWND control : controls_) setControlFont(control, nextFont.get());
            controlFont_ = std::move(nextFont);
        }
        const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
        if (suggested) {
            const RECT fitted = aoc::platform::fitToWorkArea(*suggested);
            SetWindowPos(hwnd_, nullptr, fitted.left, fitted.top,
                         fitted.right - fitted.left, fitted.bottom - fitted.top,
                                     SWP_NOZORDER | SWP_NOACTIVATE);
        }
        // A DPI notification is allowed to carry an unchanged suggested rect;
        // relayout explicitly so controls still use physical sizes for the new DPI.
        RECT client{};
        if (GetClientRect(hwnd_, &client)) layoutControls(client.right, client.bottom);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }
    case WM_SIZE:
        layoutControls(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int notification = HIWORD(wParam);
        if (id == kMonitorId && notification == CBN_SELCHANGE) {
            const int selection = static_cast<int>(SendMessageW(monitorCombo_, CB_GETCURSEL, 0, 0));
            if (selection >= 0 && static_cast<std::size_t>(selection) < monitors_.size()) {
                selectedKey_ = monitors_[selection].stableKey;
                updateSummary();
                InvalidateRect(hwnd_, &heatmapRect_, FALSE);
            }
        } else if (notification == BN_CLICKED && id == kResetId && onReset_) {
            onReset_();
        } else if (notification == BN_CLICKED && id == kExportId && onExport_) {
            onExport_();
        } else if (notification == BN_CLICKED && id == kCloseId) {
            hide();
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd_, &paint);
        paintHeatmap(dc, paint.rcPaint);
        EndPaint(hwnd_, &paint);
        return 0;
    }
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

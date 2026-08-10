#include "aoc/platform/color_picker_window.h"

#include "aoc/platform/win32_ui.h"

#include <dwmapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace aoc::platform {
namespace {

constexpr wchar_t kColorPickerClass[] = L"AdaptiveOledClockColorPicker";
constexpr int kHexEditId = 2100;
constexpr COLORREF kSurface = RGB(246, 247, 251);
constexpr COLORREF kPanel = RGB(255, 255, 255);
constexpr COLORREF kText = RGB(24, 27, 37);
constexpr COLORREF kMuted = RGB(102, 112, 133);
constexpr COLORREF kBorder = RGB(207, 212, 224);
constexpr COLORREF kAccent = RGB(91, 76, 245);
constexpr COLORREF kAccentPressed = RGB(72, 57, 222);
constexpr COLORREF kInvalid = RGB(210, 54, 73);

constexpr std::array<core::Color, 10> kPresets{{
    {255, 255, 255, 255}, {176, 176, 176, 255}, {40, 44, 55, 255},
    {224, 54, 63, 255}, {241, 132, 72, 255}, {238, 194, 90, 255},
    {46, 160, 151, 255}, {31, 171, 205, 255}, {28, 94, 173, 255},
    {204, 69, 143, 255},
}};

HBRUSH dcBrush(HDC dc, COLORREF color) {
    SetDCBrushColor(dc, color);
    return static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
}

void drawRoundRect(HDC dc, const RECT& bounds, COLORREF fill, COLORREF border, int radius,
                   int borderWidth = 1) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, std::max(1, borderWidth), border);
    const HGDIOBJ previousBrush = SelectObject(dc, brush);
    const HGDIOBJ previousPen = SelectObject(dc, pen);
    RoundRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom, radius, radius);
    SelectObject(dc, previousPen);
    SelectObject(dc, previousBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

} // namespace

ColorPickerWindow::ColorPickerWindow(HINSTANCE instance, HWND owner, core::Color initialColor)
    : instance_(instance), owner_(owner), color_(initialColor), hsv_(core::rgbToHsv(initialColor)) {}

ColorPickerWindow::~ColorPickerWindow() {
    if (hwnd_) DestroyWindow(hwnd_);
    if (bufferedPaintInitialized_) BufferedPaintUnInit();
}

std::optional<core::Color> ColorPickerWindow::choose(HINSTANCE instance, HWND owner,
                                                      core::Color initialColor) {
    ColorPickerWindow picker(instance, owner, initialColor);
    if (!picker.create()) return std::nullopt;
    return picker.run();
}

int ColorPickerWindow::scale(int value) const noexcept { return scaleDip(value, dpi_); }

bool ColorPickerWindow::create() {
    WNDCLASSEXW windowClass{sizeof(WNDCLASSEXW)};
    windowClass.hInstance = instance_;
    windowClass.lpfnWndProc = &ColorPickerWindow::windowProc;
    windowClass.lpszClassName = kColorPickerClass;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = loadApplicationIcon(instance_, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    windowClass.hIconSm = loadApplicationIcon(instance_, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    hwnd_ = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, kColorPickerClass,
                            L"Clock color", WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
                            CW_USEDEFAULT, CW_USEDEFAULT, 500, 660, owner_, nullptr, instance_, this);
    if (!hwnd_) return false;
    dpi_ = GetDpiForWindow(hwnd_);
    if (dpi_ == 0) dpi_ = 96;
    font_.reset(createControlFont(dpi_));
    titleFont_.reset(CreateFontW(-scale(22), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                 L"Segoe UI Variable Display"));
    if (!font_) return false;
    bufferedPaintInitialized_ = SUCCEEDED(BufferedPaintInit());

    hexEdit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                               ES_AUTOHSCROLL | ES_UPPERCASE,
                               0, 0, 1, 1, hwnd_,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(kHexEditId)), instance_, nullptr);
    cancelButton_ = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                    BS_OWNERDRAW, 0, 0, 1, 1, hwnd_,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), instance_, nullptr);
    acceptButton_ = CreateWindowExW(0, L"BUTTON", L"Use color", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                    BS_OWNERDRAW | BS_DEFPUSHBUTTON, 0, 0, 1, 1, hwnd_,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), instance_, nullptr);
    if (!hexEdit_ || !cancelButton_ || !acceptButton_) return false;
    for (HWND control : {hexEdit_, cancelButton_, acceptButton_}) setControlFont(control, font_.get());
    SendMessageW(hexEdit_, EM_SETLIMITTEXT, 7, 0);
    SendMessageW(hexEdit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(scale(8), scale(8)));
    SendMessageW(hwnd_, DM_SETDEFID, IDOK, 0);
    updateHexText(true);
    applyTheme();
    return true;
}

std::optional<core::Color> ColorPickerWindow::run() {
    const RECT bounds = centeredWindowRect(owner_, hwnd_, 500, 660, dpi_);
    SetWindowPos(hwnd_, HWND_TOP, bounds.left, bounds.top, bounds.right - bounds.left,
                 bounds.bottom - bounds.top, SWP_SHOWWINDOW);
    updateHexText(true);
    const bool restoreOwner = owner_ && IsWindowEnabled(owner_);
    if (restoreOwner) EnableWindow(owner_, FALSE);
    SetForegroundWindow(hwnd_);
    SetFocus(hexEdit_);

    MSG message{};
    bool sawQuit = false;
    int quitCode = 0;
    while (!finished_) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            sawQuit = result == 0;
            quitCode = static_cast<int>(message.wParam);
            break;
        }
        if (!IsDialogMessageW(hwnd_, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (hwnd_) DestroyWindow(hwnd_);
    if (restoreOwner && IsWindow(owner_)) {
        EnableWindow(owner_, TRUE);
        SetForegroundWindow(owner_);
    }
    if (sawQuit) PostQuitMessage(quitCode);
    return accepted_ ? std::optional<core::Color>{color_} : std::nullopt;
}

void ColorPickerWindow::applyTheme() {
    constexpr DWORD kCornerAttribute = 33;
    constexpr DWORD kBorderAttribute = 34;
    constexpr DWORD kCaptionAttribute = 35;
    constexpr DWORD kTextAttribute = 36;
    const int rounded = 2;
    const COLORREF border = kBorder;
    const COLORREF caption = kSurface;
    const COLORREF text = kText;
    (void)DwmSetWindowAttribute(hwnd_, static_cast<DWMWINDOWATTRIBUTE>(kCornerAttribute),
                                &rounded, sizeof(rounded));
    (void)DwmSetWindowAttribute(hwnd_, static_cast<DWMWINDOWATTRIBUTE>(kBorderAttribute),
                                &border, sizeof(border));
    (void)DwmSetWindowAttribute(hwnd_, static_cast<DWMWINDOWATTRIBUTE>(kCaptionAttribute),
                                &caption, sizeof(caption));
    (void)DwmSetWindowAttribute(hwnd_, static_cast<DWMWINDOWATTRIBUTE>(kTextAttribute),
                                &text, sizeof(text));
}

void ColorPickerWindow::layout(int width, int height) {
    width = std::max(1, width);
    height = std::max(1, height);
    const int margin = scale(24);
    const int buttonHeight = scale(40);
    const int buttonY = std::max(scale(480), height - margin - buttonHeight);
    const int presetsTop = buttonY - scale(70);
    const int swatchTop = presetsTop - scale(92);
    const int hueTop = swatchTop - scale(42);
    fieldRect_ = {margin, scale(84), std::max(margin + 1, width - margin),
                  std::max(scale(160), hueTop - scale(28))};
    hueRect_ = {margin, hueTop, std::max(margin + 1, width - margin), hueTop + scale(18)};
    swatchRect_ = {margin, swatchTop, margin + scale(56), swatchTop + scale(56)};
    hexContainer_ = {margin + scale(76), swatchTop + scale(6),
                     std::min(width - margin, margin + scale(280)), swatchTop + scale(50)};
    presetsRect_ = {margin, presetsTop, std::max(margin + 1, width - margin), presetsTop + scale(36)};

    MoveWindow(hexEdit_, hexContainer_.left + scale(8), hexContainer_.top + scale(7),
               std::max(1, static_cast<int>(hexContainer_.right - hexContainer_.left) - scale(16)),
               scale(30), TRUE);
    const int buttonWidth = scale(110);
    MoveWindow(acceptButton_, width - margin - buttonWidth, buttonY, buttonWidth, buttonHeight, TRUE);
    MoveWindow(cancelButton_, width - margin - buttonWidth * 2 - scale(10), buttonY,
               buttonWidth, buttonHeight, TRUE);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ColorPickerWindow::paintGradient(HDC dc, const RECT& bounds) const {
    const int width = std::max(1L, bounds.right - bounds.left);
    const int height = std::max(1L, bounds.bottom - bounds.top);
    if (gradientWidth_ != width || gradientHeight_ != height ||
        std::abs(gradientHue_ - hsv_.hue) > 0.001) {
        gradientWidth_ = width;
        gradientHeight_ = height;
        gradientHue_ = hsv_.hue;
        gradientPixels_.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
        const core::Color hueColor = core::hsvToRgb({hsv_.hue, 1.0, 1.0});
        const int maximumX = std::max(1, width - 1);
        const int maximumY = std::max(1, height - 1);
        for (int y = 0; y < height; ++y) {
            const int value = maximumY - y;
            for (int x = 0; x < width; ++x) {
                const int inverseSaturation = maximumX - x;
                const int redAtFullValue = (255 * inverseSaturation + hueColor.r * x) / maximumX;
                const int greenAtFullValue = (255 * inverseSaturation + hueColor.g * x) / maximumX;
                const int blueAtFullValue = (255 * inverseSaturation + hueColor.b * x) / maximumX;
                const auto red = static_cast<std::uint32_t>(redAtFullValue * value / maximumY);
                const auto green = static_cast<std::uint32_t>(greenAtFullValue * value / maximumY);
                const auto blue = static_cast<std::uint32_t>(blueAtFullValue * value / maximumY);
                gradientPixels_[static_cast<std::size_t>(y) * width + x] =
                    blue | (green << 8U) | (red << 16U);
            }
        }
    }
    BITMAPINFO bitmap{};
    bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = width;
    bitmap.bmiHeader.biHeight = -height;
    bitmap.bmiHeader.biPlanes = 1;
    bitmap.bmiHeader.biBitCount = 32;
    bitmap.bmiHeader.biCompression = BI_RGB;
    HRGN clip = CreateRoundRectRgn(bounds.left, bounds.top, bounds.right + 1, bounds.bottom + 1,
                                   scale(10), scale(10));
    SelectClipRgn(dc, clip);
    SetDIBitsToDevice(dc, bounds.left, bounds.top, width, height, 0, 0, 0, height,
                      gradientPixels_.data(), &bitmap, DIB_RGB_COLORS);
    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);
}

void ColorPickerWindow::paintHue(HDC dc, const RECT& bounds) const {
    const int width = std::max(1L, bounds.right - bounds.left);
    if (hueWidth_ != width) {
        hueWidth_ = width;
        huePixels_.resize(static_cast<std::size_t>(width));
        for (int x = 0; x < width; ++x) {
            const core::Color color = core::hsvToRgb({360.0 * x / std::max(1, width - 1), 1.0, 1.0});
            huePixels_[static_cast<std::size_t>(x)] = static_cast<std::uint32_t>(color.b) |
                (static_cast<std::uint32_t>(color.g) << 8U) |
                (static_cast<std::uint32_t>(color.r) << 16U);
        }
    }
    BITMAPINFO bitmap{};
    bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = width;
    bitmap.bmiHeader.biHeight = -1;
    bitmap.bmiHeader.biPlanes = 1;
    bitmap.bmiHeader.biBitCount = 32;
    bitmap.bmiHeader.biCompression = BI_RGB;
    HRGN clip = CreateRoundRectRgn(bounds.left, bounds.top, bounds.right + 1, bounds.bottom + 1,
                                   scale(12), scale(12));
    SelectClipRgn(dc, clip);
    StretchDIBits(dc, bounds.left, bounds.top, width, bounds.bottom - bounds.top,
                  0, 0, width, 1, huePixels_.data(), &bitmap, DIB_RGB_COLORS, SRCCOPY);
    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);
}

RECT ColorPickerWindow::presetRect(std::size_t index) const noexcept {
    const int diameter = scale(30);
    const int available = std::max(0L, presetsRect_.right - presetsRect_.left - diameter);
    const int left = presetsRect_.left + static_cast<int>(index) * available /
                     std::max(1, static_cast<int>(kPresets.size()) - 1);
    return {left, presetsRect_.top, left + diameter, presetsRect_.top + diameter};
}

void ColorPickerWindow::paint(HDC dc) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    FillRect(dc, &client, dcBrush(dc, kSurface));
    SetBkMode(dc, TRANSPARENT);

    RECT title{scale(24), scale(18), client.right - scale(24), scale(50)};
    HFONT titleFont = titleFont_ ? titleFont_.get() : font_.get();
    HGDIOBJ previousFont = SelectObject(dc, titleFont);
    SetTextColor(dc, kText);
    DrawTextW(dc, L"Choose clock color", -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, font_.get());
    RECT subtitle{scale(24), scale(52), client.right - scale(24), scale(76)};
    SetTextColor(dc, kMuted);
    DrawTextW(dc, L"Drag to mix a color, or enter a hex value.", -1, &subtitle,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    paintGradient(dc, fieldRect_);
    const int fieldX = fieldRect_.left + static_cast<int>(std::lround(hsv_.saturation *
        std::max(1L, fieldRect_.right - fieldRect_.left - 1)));
    const int fieldY = fieldRect_.top + static_cast<int>(std::lround((1.0 - hsv_.value) *
        std::max(1L, fieldRect_.bottom - fieldRect_.top - 1)));
    HPEN selectorPen = CreatePen(PS_SOLID, scale(2), RGB(255, 255, 255));
    HGDIOBJ previousPen = SelectObject(dc, selectorPen);
    HGDIOBJ previousBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Ellipse(dc, fieldX - scale(8), fieldY - scale(8), fieldX + scale(8), fieldY + scale(8));
    SelectObject(dc, previousBrush);
    SelectObject(dc, previousPen);
    DeleteObject(selectorPen);

    paintHue(dc, hueRect_);
    const int hueX = hueRect_.left + static_cast<int>(std::lround(hsv_.hue / 360.0 *
        std::max(1L, hueRect_.right - hueRect_.left - 1)));
    HBRUSH hueBrush = CreateSolidBrush(RGB(color_.r, color_.g, color_.b));
    HPEN huePen = CreatePen(PS_SOLID, scale(3), RGB(255, 255, 255));
    previousBrush = SelectObject(dc, hueBrush);
    previousPen = SelectObject(dc, huePen);
    Ellipse(dc, hueX - scale(10), (hueRect_.top + hueRect_.bottom) / 2 - scale(10),
            hueX + scale(10), (hueRect_.top + hueRect_.bottom) / 2 + scale(10));
    SelectObject(dc, previousPen);
    SelectObject(dc, previousBrush);
    DeleteObject(huePen);
    DeleteObject(hueBrush);

    const COLORREF selected = RGB(color_.r, color_.g, color_.b);
    drawRoundRect(dc, swatchRect_, selected, kBorder, scale(28), scale(2));
    const COLORREF hexBorder = hexValid_ ? (GetFocus() == hexEdit_ ? kAccent : kBorder) : kInvalid;
    drawRoundRect(dc, hexContainer_, kPanel, hexBorder, scale(10), GetFocus() == hexEdit_ ? scale(2) : scale(1));

    RECT hexLabel{hexContainer_.left, hexContainer_.top - scale(24), hexContainer_.right, hexContainer_.top};
    SetTextColor(dc, kMuted);
    DrawTextW(dc, hexValid_ ? L"Hex color" : L"Enter 3 or 6 hex digits", -1, &hexLabel,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RECT presetsLabel{presetsRect_.left, presetsRect_.top - scale(27), presetsRect_.right, presetsRect_.top};
    DrawTextW(dc, L"Quick colors", -1, &presetsLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    for (std::size_t index = 0; index < kPresets.size(); ++index) {
        const auto preset = kPresets[index];
        const RECT bounds = presetRect(index);
        const bool current = preset.r == color_.r && preset.g == color_.g && preset.b == color_.b;
        drawRoundRect(dc, bounds, RGB(preset.r, preset.g, preset.b), current ? kAccent : kBorder,
                      scale(16), current ? scale(3) : scale(1));
    }
    SelectObject(dc, previousFont);
}

void ColorPickerWindow::paintButton(const DRAWITEMSTRUCT& item) const {
    RECT bounds = item.rcItem;
    const bool primary = item.CtlID == IDOK;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const COLORREF fill = primary ? (pressed ? kAccentPressed : kAccent) :
                                   (pressed ? RGB(239, 237, 255) : kSurface);
    const COLORREF border = primary ? fill : (pressed ? kAccent : kBorder);
    drawRoundRect(item.hDC, bounds, fill, border, scale(9), scale(1));
    wchar_t text[64]{};
    GetWindowTextW(item.hwndItem, text, static_cast<int>(std::size(text)));
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, primary ? RGB(255, 255, 255) : kText);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(item.hwndItem, WM_GETFONT, 0, 0));
    const HGDIOBJ previousFont = font ? SelectObject(item.hDC, font) : nullptr;
    DrawTextW(item.hDC, text, -1, &bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (previousFont) SelectObject(item.hDC, previousFont);
}

void ColorPickerWindow::setColor(core::Color color, bool updateHex) {
    color.a = 255;
    color_ = color;
    hsv_ = core::rgbToHsv(color_);
    hexValid_ = true;
    if (updateHex) updateHexText(true);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ColorPickerWindow::setFromPoint(POINT point) {
    const int width = std::max(1L, fieldRect_.right - fieldRect_.left - 1);
    const int height = std::max(1L, fieldRect_.bottom - fieldRect_.top - 1);
    hsv_.saturation = std::clamp(static_cast<double>(point.x - fieldRect_.left) / width, 0.0, 1.0);
    hsv_.value = 1.0 - std::clamp(static_cast<double>(point.y - fieldRect_.top) / height, 0.0, 1.0);
    color_ = core::hsvToRgb(hsv_);
    hexValid_ = true;
    updateHexText();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ColorPickerWindow::setHueFromPoint(POINT point) {
    const int width = std::max(1L, hueRect_.right - hueRect_.left - 1);
    hsv_.hue = std::clamp(static_cast<double>(point.x - hueRect_.left) / width, 0.0, 1.0) * 360.0;
    if (hsv_.hue >= 360.0) hsv_.hue = 359.999;
    color_ = core::hsvToRgb(hsv_);
    hexValid_ = true;
    updateHexText();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ColorPickerWindow::updateHexText(bool force) {
    if (!hexEdit_) return;
    const ULONGLONG now = GetTickCount64();
    if (!force && now - lastHexUpdateTick_ < 32) return;
    lastHexUpdateTick_ = now;
    syncingHex_ = true;
    const std::wstring text = wideFromUtf8(core::colorToHex(color_));
    SetWindowTextW(hexEdit_, text.c_str());
    SendMessageW(hexEdit_, EM_SETSEL, text.size(), text.size());
    syncingHex_ = false;
}

void ColorPickerWindow::readHexText() {
    if (syncingHex_ || !hexEdit_) return;
    wchar_t buffer[32]{};
    GetWindowTextW(hexEdit_, buffer, static_cast<int>(std::size(buffer)));
    std::string text;
    for (const wchar_t value : std::wstring_view(buffer)) {
        if (value == L'\0') break;
        if (value > 0x7F) { hexValid_ = false; InvalidateRect(hwnd_, nullptr, FALSE); return; }
        text.push_back(static_cast<char>(value));
    }
    const auto parsed = core::parseHexColor(text);
    hexValid_ = parsed.has_value();
    if (parsed) setColor(*parsed, false);
    else InvalidateRect(hwnd_, nullptr, FALSE);
}

void ColorPickerWindow::finish(bool accepted) {
    readHexText();
    if (accepted && !hexValid_) {
        MessageBeep(MB_ICONWARNING);
        SetFocus(hexEdit_);
        return;
    }
    accepted_ = accepted;
    finished_ = true;
    ShowWindow(hwnd_, SW_HIDE);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
}

LRESULT CALLBACK ColorPickerWindow::windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<ColorPickerWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<ColorPickerWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = window;
    }
    if (!self) return DefWindowProcW(window, message, wParam, lParam);
    const LRESULT result = self->handleMessage(message, wParam, lParam);
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        self->hwnd_ = nullptr;
    }
    return result;
}

LRESULT ColorPickerWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_SIZE:
        layout(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize = {scale(440), scale(590)};
        return 0;
    }
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wParam) ? HIWORD(wParam) : 96;
        UniqueGdiFont nextFont(createControlFont(dpi_));
        UniqueGdiFont nextTitle(CreateFontW(-scale(22), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                            L"Segoe UI Variable Display"));
        if (nextFont) {
            font_ = std::move(nextFont);
            for (HWND control : {hexEdit_, cancelButton_, acceptButton_}) setControlFont(control, font_.get());
        }
        if (nextTitle) titleFont_ = std::move(nextTitle);
        const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
        if (suggested) {
            const RECT fitted = fitToWorkArea(*suggested);
            SetWindowPos(hwnd_, nullptr, fitted.left, fitted.top, fitted.right - fitted.left,
                         fitted.bottom - fitted.top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paintInfo{};
        HDC dc = BeginPaint(hwnd_, &paintInfo);
        RECT client{};
        GetClientRect(hwnd_, &client);
        HDC bufferedDc = nullptr;
        HPAINTBUFFER buffer = BeginBufferedPaint(dc, &client, BPBF_COMPATIBLEBITMAP, nullptr, &bufferedDc);
        if (buffer && bufferedDc) {
            paint(bufferedDc);
            EndBufferedPaint(buffer, TRUE);
        } else {
            if (buffer) EndBufferedPaint(buffer, FALSE);
            paint(dc);
        }
        EndPaint(hwnd_, &paintInfo);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, kPanel);
        SetTextColor(dc, kText);
        return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    }
    case WM_DRAWITEM: {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (item && item->CtlType == ODT_BUTTON) {
            paintButton(*item);
            return TRUE;
        }
        break;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int notification = HIWORD(wParam);
        if (id == kHexEditId && notification == EN_CHANGE) readHexText();
        else if (id == kHexEditId && notification == EN_SETFOCUS) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (id == kHexEditId && notification == EN_KILLFOCUS) {
            if (!hexValid_) {
                hexValid_ = true;
                updateHexText(true);
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (id == IDOK && notification == BN_CLICKED) finish(true);
        else if (id == IDCANCEL && notification == BN_CLICKED) finish(false);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (PtInRect(&fieldRect_, point)) {
            draggingField_ = true;
            SetFocus(hwnd_);
            SetCapture(hwnd_);
            setFromPoint(point);
            return 0;
        }
        if (PtInRect(&hueRect_, point)) {
            draggingHue_ = true;
            SetFocus(hwnd_);
            SetCapture(hwnd_);
            setHueFromPoint(point);
            return 0;
        }
        for (std::size_t index = 0; index < kPresets.size(); ++index) {
            const RECT bounds = presetRect(index);
            if (PtInRect(&bounds, point)) {
                setColor(kPresets[index], true);
                return 0;
            }
        }
        break;
    }
    case WM_MOUSEMOVE:
        if (draggingField_) setFromPoint({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        else if (draggingHue_) setHueFromPoint({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        return 0;
    case WM_LBUTTONUP:
        updateHexText(true);
        draggingField_ = false;
        draggingHue_ = false;
        if (GetCapture() == hwnd_) ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        updateHexText(true);
        draggingField_ = false;
        draggingHue_ = false;
        return 0;
    case WM_SETCURSOR: {
        POINT point{};
        GetCursorPos(&point);
        ScreenToClient(hwnd_, &point);
        if (PtInRect(&fieldRect_, point) || PtInRect(&hueRect_, point)) {
            SetCursor(LoadCursorW(nullptr, IDC_CROSS));
            return TRUE;
        }
        break;
    }
    case WM_CLOSE:
        finish(false);
        return 0;
    case WM_DESTROY:
        finished_ = true;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

} // namespace aoc::platform

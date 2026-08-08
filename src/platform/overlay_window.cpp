#include "aoc/platform/overlay_window.h"

#include <windowsx.h>

namespace aoc::platform {
namespace {
constexpr wchar_t kOverlayClass[] = L"AdaptiveOledClockOverlayWindow";
}

OverlayWindow::~OverlayWindow() { destroy(); }

bool OverlayWindow::create(HINSTANCE instance, DragCallback onDragged, SimpleCallback onDragFinished,
                           SimpleCallback onEscape, DpiCallback onDpiChanged) {
    if (hwnd_) return true;
    instance_ = instance;
    onDragged_ = std::move(onDragged);
    onDragFinished_ = std::move(onDragFinished);
    onEscape_ = std::move(onEscape);
    onDpiChanged_ = std::move(onDpiChanged);
    WNDCLASSEXW windowClass{sizeof(WNDCLASSEXW)};
    windowClass.hInstance = instance_;
    windowClass.lpfnWndProc = &OverlayWindow::windowProc;
    windowClass.lpszClassName = kOverlayClass;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&windowClass);
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                            kOverlayClass, L"Adaptive OLED Clock", WS_POPUP, 0, 0, 1, 1,
                            nullptr, nullptr, instance_, this);
    return hwnd_ != nullptr;
}

void OverlayWindow::destroy() {
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
}

void OverlayWindow::show() {
    if (!hwnd_) return;
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
}

void OverlayWindow::hide() {
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

void OverlayWindow::moveResize(RECT screenRect) {
    if (!hwnd_) return;
    SetWindowPos(hwnd_, HWND_TOPMOST, screenRect.left, screenRect.top,
                 screenRect.right - screenRect.left, screenRect.bottom - screenRect.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void OverlayWindow::setInteractive(bool interactive) {
    if (!hwnd_ || interactive_ == interactive) return;
    interactive_ = interactive;
    LONG_PTR styles = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    if (interactive_) {
        styles &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
    } else {
        styles |= WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
    }
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, styles);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    if (!interactive_) {
        ReleaseCapture();
        dragging_ = false;
    }
}

RECT OverlayWindow::bounds() const noexcept {
    RECT rect{};
    if (hwnd_) GetWindowRect(hwnd_, &rect);
    return rect;
}

LRESULT CALLBACK OverlayWindow::windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<OverlayWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = window;
    }
    return self ? self->handleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT OverlayWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_NCHITTEST:
        return interactive_ ? HTCLIENT : HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_LBUTTONDOWN:
        if (interactive_) {
            dragging_ = true;
            dragOffset_.x = GET_X_LPARAM(lParam);
            dragOffset_.y = GET_Y_LPARAM(lParam);
            SetCapture(hwnd_);
            return 0;
        }
        break;
    case WM_MOUSEMOVE:
        if (interactive_ && dragging_ && (wParam & MK_LBUTTON)) {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ClientToScreen(hwnd_, &point);
            point.x -= dragOffset_.x;
            point.y -= dragOffset_.y;
            if (onDragged_) onDragged_(point);
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (dragging_) {
            dragging_ = false;
            ReleaseCapture();
            if (onDragFinished_) onDragFinished_();
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (interactive_ && wParam == VK_ESCAPE) {
            if (onEscape_) onEscape_();
            return 0;
        }
        break;
    case WM_DPICHANGED:
        if (onDpiChanged_) onDpiChanged_(HIWORD(wParam));
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

} // namespace aoc::platform

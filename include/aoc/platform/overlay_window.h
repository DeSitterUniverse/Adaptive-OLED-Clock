#pragma once

#include <windows.h>

#include <functional>

namespace aoc::platform {

class OverlayWindow {
public:
    using DragCallback = std::function<void(POINT)>;
    using SimpleCallback = std::function<void()>;
    using DpiCallback = std::function<void(UINT)>;

    OverlayWindow() = default;
    ~OverlayWindow();

    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    [[nodiscard]] bool create(HINSTANCE instance, DragCallback onDragged, SimpleCallback onDragFinished,
                              SimpleCallback onEscape, DpiCallback onDpiChanged = {});
    void destroy();
    void show();
    void hide();
    void moveResize(RECT screenRect);
    void setInteractive(bool interactive);
    [[nodiscard]] bool interactive() const noexcept { return interactive_; }
    [[nodiscard]] HWND hwnd() const noexcept { return hwnd_; }
    [[nodiscard]] RECT bounds() const noexcept;

private:
    static LRESULT CALLBACK windowProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    HINSTANCE instance_{nullptr};
    HWND hwnd_{nullptr};
    bool interactive_{false};
    bool dragging_{false};
    POINT dragOffset_{};
    DragCallback onDragged_;
    SimpleCallback onDragFinished_;
    SimpleCallback onEscape_;
    DpiCallback onDpiChanged_;
};

} // namespace aoc::platform

#pragma once

#include <windows.h>

#include <utility>

namespace aoc::platform {

template <typename Handle, auto Close>
class UniqueWin32Handle {
public:
    UniqueWin32Handle() = default;
    explicit UniqueWin32Handle(Handle handle) noexcept : handle_(handle) {}
    ~UniqueWin32Handle() { reset(); }

    UniqueWin32Handle(const UniqueWin32Handle&) = delete;
    UniqueWin32Handle& operator=(const UniqueWin32Handle&) = delete;

    UniqueWin32Handle(UniqueWin32Handle&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)) {}

    UniqueWin32Handle& operator=(UniqueWin32Handle&& other) noexcept {
        if (this != &other) reset(std::exchange(other.handle_, nullptr));
        return *this;
    }

    [[nodiscard]] Handle get() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }

    Handle release() noexcept { return std::exchange(handle_, nullptr); }

    void reset(Handle next = nullptr) noexcept {
        if (handle_ == next) return;
        if (handle_) Close(handle_);
        handle_ = next;
    }

private:
    Handle handle_{nullptr};
};

inline void closeKernelHandle(HANDLE handle) noexcept { CloseHandle(handle); }
inline void deleteGdiFont(HFONT font) noexcept { DeleteObject(font); }
inline void unhookWinEvent(HWINEVENTHOOK hook) noexcept { UnhookWinEvent(hook); }
inline void unregisterPowerNotification(HPOWERNOTIFY notification) noexcept {
    UnregisterPowerSettingNotification(notification);
}

using UniqueKernelHandle = UniqueWin32Handle<HANDLE, closeKernelHandle>;
using UniqueGdiFont = UniqueWin32Handle<HFONT, deleteGdiFont>;
using UniqueWinEventHook = UniqueWin32Handle<HWINEVENTHOOK, unhookWinEvent>;
using UniquePowerNotification = UniqueWin32Handle<HPOWERNOTIFY, unregisterPowerNotification>;

} // namespace aoc::platform

# Adaptive OLED Clock

Adaptive OLED Clock is a native Windows desktop clock for OLED displays. It keeps the current time visible in a transparent, click-through overlay while varying the clock's position so that exposure is not concentrated in one fixed area of the screen. The application is written in C++20 with Win32, Direct2D, and DirectWrite. The app aims to be minimal and performant. I created this app so I could have the time always up while also auto-hiding my taskbar, but it's good for anyone who wants to have a customizable digital clock on OLED monitors with user-adjustable mitigations for burn in,

## Features

- Transparent, topmost, non-activating, click-through clock overlay with tray-resident operation.
- Locale-derived, 12-hour, or 24-hour time; optional AM/PM, seconds, and locale-formatted date.
- Configurable font family, weight, size, text color, and normal opacity.
- Temporary brightness from the tray or settings, with a configurable boost opacity and automatic restoration.
- Slow scheduled movement with an edge-only default profile: white at 80% opacity, one move per hour, three 3 px small shifts, and no gap from the screen edge. Four-corners, whole-screen, and anchored local-area modes are also available.
- Hours, minutes, and seconds for any positive movement interval supported by the settings format, plus allowed movement area, edge gap, monitor selection, and manual positioning controls.
- Independent 12-by-8 exposure history for each recognized monitor.
- Exposure heatmaps and summary statistics, with reset and CSV export actions.
- Fullscreen hiding plus handling for monitor changes, DPI changes, time and time-zone changes, display power, lock/unlock, and sleep/resume.
- Optional launch at Windows startup and the default `Ctrl+Alt+C` visibility hotkey.
- Single-instance behavior, tray recovery, atomic local persistence, and corruption recovery for local data.
- No telemetry, network calls, account, or remote synchronization.

## Movement model

The clock uses a small, practical exposure model rather than a panel-specific lifetime prediction:

1. Each monitor is represented by a 12-by-8 grid.
2. While the clock is visible and the display/session policy allows charging, each visible interval is added to the cells covered by the clock. Cells receive exposure in proportion to the clock rectangle's overlap with them.
3. When the clock moves, its selected movement mode defines the possible positions. Recent positions are avoided first; saved screen-use history is only a gentle tie-breaker. The default edge-only mode keeps the clock around the usable screen perimeter.

History is maintained independently for each physical monitor, so one display does not affect another display's placement decisions. The configured number of optional small shifts is divided evenly across each major-movement interval. Second-by-second clock redraws never invoke placement. Position clock stores an anchor only for Local area; it is not a global startup position.

## Requirements

- A 64-bit Windows desktop environment.
- CMake and a C++20-capable Visual Studio/MSVC toolchain for building.
- Windows' built-in Win32, Direct2D, and DirectWrite components.

## Building

Open a Developer PowerShell with CMake and the Visual Studio C++ tools available, then run from this directory:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

The Release executable is:

```text
build\Release\adaptive_oled_clock.exe
```

## Running and using the clock

Run `build\Release\adaptive_oled_clock.exe`. The application stays in the Windows notification area while the clock overlay is shown on the selected monitor. The overlay does not take focus or intercept normal mouse clicks. If enabled, `Ctrl+Alt+C` toggles visibility.

The clock responds to real minute boundaries and relevant Windows events. It hides during detected fullscreen use when that setting is enabled, pauses exposure charging when the display is off or the session is locked, and re-evaluates its monitor, DPI, position, and time display after system changes.

Use the tray menu to show or hide the clock, activate temporary brightness, choose a movement mode or opacity preset, open Settings or Movement History, start drag-to-position mode, or exit. Drag-to-position also sets the center used by Local area without creating a global startup position.

A second launch hands off to the existing instance and opens Settings instead of creating another clock. If Windows Explorer restarts, the application recreates its notification-area icon.

## Settings and tray basics

The Settings window groups controls into three areas:

- **Clock** — time format, AM/PM, seconds, date, font family, font weight, font size, color, normal opacity, and temporary brightness values.
- **Movement** — hours/minutes/seconds between scheduled moves, optional small-shift count and distance, edge-only/four-corners/whole-screen/local-area mode, the Local area radius, screen-edge gap, and allowed area.
- **Display & Windows** — follow-primary or fixed-monitor selection, fullscreen hiding, startup registration, and the global hotkey.

Settings remain unchanged until **Apply** is selected. **Movement history** at the bottom opens the separate monitor history window, where history can be viewed, cleared, or exported as CSV. Movement history itself is checkpointed periodically and when the app closes.

## Data and privacy

Local application data is stored under:

```text
%LOCALAPPDATA%\AdaptiveOledClockCpp\
```

The directory contains:

- `settings.conf` — versioned clock and movement settings.
- `exposure.dat` — versioned per-monitor exposure history.
- `app.log` — a low-volume local lifecycle, configuration, movement, and recovery log.

The application does not transmit this data or make network requests. Writes use a temporary sibling file and replacement so an interrupted write can be recovered safely. Invalid existing data is quarantined before defaults are used.

## Architecture

The project separates platform-independent clock and placement logic from the Windows host:

- `aoc_core` contains time formatting, settings validation and migration, geometry, monitor identity data, exposure maps and tracking, and candidate generation/scoring.
- The Win32 host owns monitor and session integration, fullscreen detection, startup registration, hotkeys, tray menus, settings and statistics windows, persistence, logging, and application lifecycle.
- The overlay is rendered with Direct2D and DirectWrite into a premultiplied bitmap and presented as a layered window with `UpdateLayeredWindow`.
- Exposure coordinates are normalized to each monitor, while rendering and DPI-aware placement use physical pixels and device-independent units as appropriate.

## Limitations

Adaptive OLED Clock is an exposure-balancing heuristic, not a burn-in prevention system or a guarantee of panel longevity. It does not model the physical aging characteristics of a particular OLED panel, measure per-pixel wear, or guarantee uniform exposure. The 12-by-8 history is intentionally coarse, and monitor identity can change when Windows reports a display differently.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for the full text.

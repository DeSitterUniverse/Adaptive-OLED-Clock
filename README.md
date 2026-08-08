# Adaptive OLED Clock (native C++ Phase 1)

Adaptive OLED Clock is a small, native Windows desktop clock designed to reduce static OLED wear while remaining unobtrusive. This repository contains the standalone C++20/Win32 implementation of Phase 1 only. It has no web UI, Electron runtime, telemetry, or third-party UI/JSON/testing framework.

## Build and run

From a Developer PowerShell with Visual Studio Build Tools and CMake available:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\adaptive_oled_clock.exe
```

The clean build uses the installed Visual Studio generator. The application is a tray-resident process; its settings window is opened from the tray menu. The global default hotkey is `Ctrl+Alt+C`.

## Phase 1 feature summary

- Native Unicode Win32 popup overlay rendered with Direct2D and DirectWrite into a premultiplied DIB and presented with `UpdateLayeredWindow`.
- Per-monitor-V2 DPI awareness, DPI-sharp normal-weight 32 DIP text, transparent background, topmost/click-through/non-activating overlay, no taskbar or Alt+Tab entry.
- Locale-derived time format with explicit 12-hour and 24-hour choices, optional AM/PM, no seconds, and no date.
- OLED-safe defaults: `#B0B0B0` text, 0.32 normal opacity, 5-minute major movement, one-minute micro-shifts up to +/-2 DIP, 24 DIP edge margin, fullscreen hiding enabled, primary-monitor following, startup disabled, and `Ctrl+Alt+C` enabled.
- Real 12x8 exposure grid per stable monitor identity. Exposure is charged using monotonic elapsed time and intersection-area weighting only while the clock is effectively visible, the display/session permits charging, and fullscreen policy allows it.
- Dynamic whole-screen lattice/anchor placement and local-wander candidates, normalized allowed/excluded areas, edge margins, recent-position/overlap penalties, deterministic low-noise tie breaking, and immediate-repeat avoidance.
- Follow-primary or fixed-monitor selection with safe fallback to primary when a fixed monitor is missing.
- One-shot scheduling against the next real system minute boundary; immediate handling of time/timezone, display, DPI, power, suspend/resume, session, and foreground/location changes.
- Tray actions for visibility, temporary brightness, movement mode, opacity presets, settings, a lightweight exposure summary, drag positioning, and exit.
- Modeless standard Win32 settings UI with live persistence for every Phase 1 setting, exclusion add/remove/clear controls, reset/default and OLED-safe preset actions, and ChooseColor.
- Temporary brightness boost defaults to 0.85 opacity for 15 seconds and restores the configured normal opacity automatically.
- HKCU Run startup registration, WTS session notifications, console/display power notifications, WinEvent foreground/location hooks, atomic temp-file replacement, corruption quarantine, and lightweight lifecycle/config/state logging.

## Architecture

`aoc_core` has no HWND or rendering dependency. It contains:

- geometry and normalized/physical/DIP conversions;
- versioned settings with migration/default validation;
- minute-boundary/time-format decisions;
- exposure maps, stores, persistence serialization, and monotonic visibility tracking;
- monitor selection and stable identity data contracts;
- fullscreen geometry heuristic;
- placement generation, scoring, exclusions, margins, recent-position avoidance, and local wander.

The Win32 host contains the D2D/DirectWrite renderer, layered overlay, tray integration, modeless settings controls, monitor enumeration, power/session/startup integration, WinEvent marshaling, persistence, logging, and application lifecycle. The startup order is load settings/exposure, resolve a monitor and placement, initialize system events/tray, then show only when policy permits.

## OLED model

The exposure grid is an intentionally small wear model rather than a panel-specific lifetime prediction. Each rendered clock rectangle is converted to monitor-normalized coordinates. For every elapsed visible interval, each intersected grid cell receives a share of elapsed time proportional to its overlap area. Placement then minimizes the cumulative weighted exposure of candidate rectangles, with strong recent-position penalties and a small random tie-breaker. This balances movement without claiming to model a particular OLED panel's physical aging curve.

## Settings, exposure, and logs

The application stores data under:

`%LOCALAPPDATA%\AdaptiveOledClockCpp\`

- `settings.conf` — documented versioned key/value settings format;
- `exposure.dat` — versioned per-monitor 12x8 exposure maps;
- `app.log` — low-volume startup, configuration, state, movement, recovery, and shutdown log.

Settings and exposure writes use a sibling `.tmp` file followed by `MoveFileExW` with replace/write-through flags. Malformed existing files are renamed with a `.corrupt-<timestamp>` suffix before safe defaults are used when possible.

## Tests

The UI-independent test executable covers time-boundary/12-or-24-hour decisions, weighted visible-only exposure and independent monitor maps, exposure round-trips and recovery, placement validity/least-used behavior/repeat avoidance/local wander, mixed-DPI geometry, missing-monitor fallback, fullscreen positive/negative cases, and versioned settings round-trip/migration/recovery.

```powershell
ctest --test-dir build -C Release --output-on-failure
```

## Phase 1 limitations

This is a practical Phase 1 OLED-safety heuristic, not a panel lifetime guarantee. It does not implement Phase 2 analytics, historical dashboards, per-pixel calibration, remote synchronization, telemetry, or a browser-based interface. The stable monitor key uses the Windows display-device identity when available and falls back to the display name when Windows does not provide a device identifier.

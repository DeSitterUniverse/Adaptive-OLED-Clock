cmake_minimum_required(VERSION 3.22)

project(AdaptiveOledClockCpp VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(aoc_core STATIC
    src/core/types.cpp
    src/core/geometry.cpp
    src/core/settings.cpp
    src/core/settings_change.cpp
    src/core/clock.cpp
    src/core/exposure.cpp
    src/core/placement.cpp
    src/core/monitor.cpp
    src/core/fullscreen.cpp
)
target_include_directories(aoc_core PUBLIC include)
target_compile_features(aoc_core PUBLIC cxx_std_20)

if (MSVC)
    target_compile_options(aoc_core PRIVATE /W4 /permissive-)
endif()

add_executable(adaptive_oled_clock WIN32
    resources/app.rc
    src/main.cpp
    src/platform/app.cpp
    src/platform/persistence.cpp
    src/platform/logging.cpp
    src/platform/renderer.cpp
    src/platform/overlay_window.cpp
    src/platform/monitor_service.cpp
    src/platform/fullscreen_service.cpp
    src/platform/startup.cpp
    src/platform/win32_ui.cpp
    src/platform/settings_window.cpp
    src/platform/statistics_window.cpp
)
target_include_directories(adaptive_oled_clock PRIVATE include resources)
target_compile_definitions(adaptive_oled_clock PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
target_link_libraries(adaptive_oled_clock PRIVATE
    aoc_core
    advapi32
    comctl32
    comdlg32
    d2d1
    dwrite
    dwmapi
    gdi32
    ole32
    powrprof
    shcore
    shell32
    user32
    uxtheme
    wtsapi32
)
if (MSVC)
    target_compile_options(adaptive_oled_clock PRIVATE /W4 /permissive- /utf-8)
    # The application manifest is embedded by resources/app.rc so the visual-
    # styles dependency is deterministic and does not collide with MSVC's
    # generated default manifest.
    target_link_options(adaptive_oled_clock PRIVATE /MANIFEST:NO)
endif()

enable_testing()
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_main.cpp")
    add_executable(aoc_core_tests tests/test_main.cpp)
    target_link_libraries(aoc_core_tests PRIVATE aoc_core)
    target_include_directories(aoc_core_tests PRIVATE include)
    if (MSVC)
        target_compile_options(aoc_core_tests PRIVATE /W4 /permissive-)
    endif()
    add_test(NAME aoc_core_tests COMMAND aoc_core_tests)
endif()

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT adaptive_oled_clock)

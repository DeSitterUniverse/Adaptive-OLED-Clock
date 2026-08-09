#include "aoc/platform/app.h"

#include <windows.h>

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand) {
    // The private --settings switch makes isolated manual QA deterministic while tray access remains the normal path.
    const bool openSettings = commandLine && wcsstr(commandLine, L"--settings") != nullptr;
    aoc::platform::App app(instance, showCommand, openSettings);
    return app.run();
}

#include "aoc/platform/app.h"

#include <windows.h>

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    aoc::platform::App app(instance, showCommand);
    return app.run();
}

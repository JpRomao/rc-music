#include "adapters/WindowsNativeHostWatchdog.hpp"

#include <qt_windows.h>
#include <string_view>

int main(int argc, char** argv) {
    if (argc != 2)
        return 3;
    music_player::m0::WindowsNativeHostWatchdog watchdog{100};
    if (!watchdog.valid())
        return 3;
    const std::string_view mode{argv[1]};
    if (mode == "finish")
        return 0;
    if (mode == "stall")
        Sleep(INFINITE);
    if (mode == "late-teardown") {
        struct StalledResource final {
            ~StalledResource() { Sleep(INFINITE); }
        } resource;
        return 0;
    }
    return 3;
}

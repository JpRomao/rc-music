#include "WindowsDownloadNetworkWorker.hpp"

#include <atomic>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

// The only cross-CRT ABI is an integer result. No strings, buffers, Qt objects,
// exceptions or allocation ownership cross this boundary. The QRC is linked
// directly into this DLL and registered before this export can be called.
extern "C" __declspec(dllexport) int WINAPI rcMusicDownloadNetworkMain() noexcept {
    static std::atomic_flag entered = ATOMIC_FLAG_INIT;
    if (entered.test_and_set(std::memory_order_acq_rel))
        return 17;
    try {
        char executable[] = "rcmusic-download-network";
        char* arguments[] = {executable, nullptr};
        return music_player::m0::runWindowsDownloadNetworkWorker(1, arguments);
    } catch (...) {
        // No private diagnostic data or C++ exception escapes to the bootstrap.
        return 17;
    }
}

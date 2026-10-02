#include <array>
#include <bit>
#include <cstddef>
#include <iterator>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace {
constexpr int failureExitCode = 17;
constexpr wchar_t payloadLeaf[] = L"rcmusic-download-network.dll";
using PayloadMain = int(WINAPI*)() noexcept;

bool payloadPath(std::array<wchar_t, 32768>& path) noexcept {
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length < 4 || static_cast<std::size_t>(length) >= path.size())
        return false;
    // The parent authenticates a local, normalized bundle before launch. Do not
    // use argv[0], the working directory, PATH, a namespace path or a UNC share
    // as an alternative source of payload location.
    const bool drive = (path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z');
    if (!drive || path[1] != L':' || path[2] != L'\\')
        return false;
    std::size_t separator = 2;
    for (std::size_t index = 3; index < static_cast<std::size_t>(length); ++index) {
        if (path[index] == L'\\')
            separator = index;
    }
    const auto directory_length = separator + 1;
    if (std::size(payloadLeaf) > path.size() - directory_length)
        return false;
    for (std::size_t index = 0; index < std::size(payloadLeaf); ++index)
        path[directory_length + index] = payloadLeaf[index];
    return true;
}
} // namespace

int wmain(int argc, wchar_t*[]) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (argc != 1 || !SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32) || !SetDllDirectoryW(L""))
        return failureExitCode;
    std::array<wchar_t, 32768> path{};
    if (!payloadPath(path))
        return failureExitCode;
    // This OS-only bootstrap restricts resolution before the first Qt import.
    // It does not authenticate files itself: the parent's artifact lease is a
    // precondition, including DLL closure and absence of loader redirections.
    const HMODULE payload =
        LoadLibraryExW(path.data(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (payload == nullptr)
        return failureExitCode;
    const auto address = GetProcAddress(payload, "rcMusicDownloadNetworkMain");
    if (address == nullptr)
        return failureExitCode;
    const auto entry = std::bit_cast<PayloadMain>(address);
    // Keep this one module resident until process termination. Qt globals,
    // message handlers and registered resources can outlive the entry call;
    // FreeLibrary here would leave callbacks pointing to unloaded code.
    return entry();
}

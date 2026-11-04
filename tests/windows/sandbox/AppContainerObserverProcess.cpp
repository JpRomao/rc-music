#include "AppContainerObserverProcess.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <array>
#include <new>

namespace music_player::sandbox_probe {
AppContainerObserverProcess::AppContainerObserverProcess(const std::uint32_t pid) noexcept { snapshot_.pid = pid; }
AppContainerObserverProcess::~AppContainerObserverProcess() {
    if (handle_)
        CloseHandle(reinterpret_cast<HANDLE>(handle_));
}
const AppContainerObserverProcess::Snapshot& AppContainerObserverProcess::open() noexcept {
    if (attempted_)
        return snapshot_;
    attempted_ = true;
    if (!snapshot_.pid) {
        snapshot_.error = ERROR_INVALID_PARAMETER;
        return snapshot_;
    }
    const auto handle = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, snapshot_.pid);
    if (!handle) {
        snapshot_.error = GetLastError();
        return snapshot_;
    }
    // Keep this exact noninheritable object handle even if a later query fails.
    // Never reopen by PID or replace the snapshot after this attempt.
    handle_ = reinterpret_cast<std::uintptr_t>(handle);
    try {
        const auto actual_pid = GetProcessId(handle);
        if (!actual_pid) {
            snapshot_.error = GetLastError();
            return snapshot_;
        }
        if (actual_pid != snapshot_.pid) {
            snapshot_.error = ERROR_INVALID_DATA;
            return snapshot_;
        }
        FILETIME creation{}, exit{}, kernel{}, user{};
        if (!GetProcessTimes(handle, &creation, &exit, &kernel, &user)) {
            snapshot_.error = GetLastError();
            return snapshot_;
        }
        snapshot_.creation_filetime = (std::uint64_t{creation.dwHighDateTime} << 32) | creation.dwLowDateTime;
        if (!snapshot_.creation_filetime) {
            snapshot_.error = ERROR_INVALID_DATA;
            return snapshot_;
        }
        std::array<wchar_t, maximum_image_path_units + 1> image{};
        DWORD length = static_cast<DWORD>(image.size());
        if (!QueryFullProcessImageNameW(handle, 0, image.data(), &length)) {
            snapshot_.error = GetLastError();
            return snapshot_;
        }
        if (!length || length > maximum_image_path_units || image[length] != L'\0' ||
            std::find(image.begin(), image.begin() + length, L'\0') != image.begin() + length) {
            snapshot_.error = ERROR_INVALID_DATA;
            return snapshot_;
        }
        snapshot_.image_path.assign(image.data(), length);
        snapshot_.token = AppContainerObserverIdentity::captureProcessToken(handle_);
        if (!snapshot_.token.complete) {
            snapshot_.error = snapshot_.token.error;
            return snapshot_;
        }
        snapshot_.error = ERROR_SUCCESS;
        snapshot_.complete = true;
    } catch (const std::bad_alloc&) {
        snapshot_.error = ERROR_NOT_ENOUGH_MEMORY;
    } catch (...) {
        snapshot_.error = ERROR_GEN_FAILURE;
    }
    return snapshot_;
}
const AppContainerObserverProcess::Snapshot& AppContainerObserverProcess::snapshot() const noexcept {
    return snapshot_;
}
AppContainerObserverProcess::Poll AppContainerObserverProcess::poll() const noexcept {
    if (!handle_)
        return {State::Unknown, ERROR_INVALID_HANDLE, std::nullopt};
    const auto handle = reinterpret_cast<HANDLE>(handle_);
    const auto waited = WaitForSingleObject(handle, 0);
    if (waited == WAIT_TIMEOUT)
        return {State::Running, ERROR_SUCCESS, std::nullopt};
    if (waited == WAIT_OBJECT_0) {
        DWORD code{};
        if (!GetExitCodeProcess(handle, &code))
            return {State::Unknown, GetLastError(), std::nullopt};
        // The process object is signaled. Even 259 is now a real exit code.
        return {State::Stopped, ERROR_SUCCESS, code};
    }
    return {State::Unknown, waited == WAIT_FAILED ? GetLastError() : ERROR_INVALID_DATA, std::nullopt};
}
} // namespace music_player::sandbox_probe

#include "AppContainerRegistryLocationNative.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <userenv.h>
#include <winternl.h>

#include <array>
#include <cstring>
#include <memory>
#include <utility>

extern "C" NTSYSAPI NTSTATUS NTAPI NtQueryKey(HANDLE key, int information_class, PVOID buffer, ULONG length,
                                              PULONG result_length);

namespace music_player::sandbox_probe {
namespace {
using Query = AppContainerRegistryLocationQuery;
static_assert(sizeof(wchar_t) == 2);
static_assert(KEY_READ == Query::key_read);

struct TokenCapture final {
    Query::Token result{};
    HANDLE process_token{};

    bool fail(const DWORD error, const TOKEN_INFORMATION_CLASS kind) {
        result.token_error = error;
        result.failed_information_class = static_cast<std::uint32_t>(kind);
        return false;
    }
    template <typename T> bool fixed(const TOKEN_INFORMATION_CLASS kind, T& output) {
        DWORD used{};
        if (!GetTokenInformation(process_token, kind, &output, sizeof(output), &used))
            return fail(GetLastError(), kind);
        return used == sizeof(output) || fail(ERROR_INVALID_DATA, kind);
    }
    template <std::size_t N>
    bool variable(const TOKEN_INFORMATION_CLASS kind, std::array<std::uint8_t, N>& output, DWORD& used) {
        if (!GetTokenInformation(process_token, kind, output.data(), static_cast<DWORD>(output.size()), &used))
            return fail(GetLastError(), kind);
        return used <= output.size() || fail(ERROR_INVALID_DATA, kind);
    }
    template <std::size_t N>
    bool sid(const std::array<std::uint8_t, N>& buffer, const DWORD used, const PSID pointer, Query::Token& destination,
             const bool package) {
        const auto begin = reinterpret_cast<std::uintptr_t>(buffer.data());
        const auto address = reinterpret_cast<std::uintptr_t>(pointer);
        const auto kind = package ? TokenAppContainerSid : TokenIntegrityLevel;
        if (!pointer || address < begin || address - begin > used || used - (address - begin) < 8)
            return fail(ERROR_INVALID_DATA, kind);
        const auto* bytes = buffer.data() + (address - begin);
        const auto count = bytes[1];
        const auto length = 8 + 4 * std::size_t{count};
        if (bytes[0] != 1 || count > 15 || length > used - (address - begin))
            return fail(ERROR_INVALID_DATA, kind);
        if (package) {
            if (length != 40)
                return fail(ERROR_INVALID_DATA, kind);
            destination.package_sid.assign(bytes, bytes + length);
        } else {
            constexpr std::array<std::uint8_t, 8> integrity_header{1, 1, 0, 0, 0, 0, 0, 16};
            if (length != 12 || std::memcmp(bytes, integrity_header.data(), integrity_header.size()) != 0)
                return fail(ERROR_INVALID_DATA, kind);
            std::memcpy(&destination.integrity_rid, bytes + 8, 4);
        }
        return true;
    }
    bool read() {
        TOKEN_TYPE type{};
        DWORD container{}, ui_access{};
        TOKEN_ELEVATION elevation{};
        if (!fixed(TokenType, type) || !fixed(TokenIsAppContainer, container) || !fixed(TokenUIAccess, ui_access) ||
            !fixed(TokenElevation, elevation))
            return false;
        result.primary = type == TokenPrimary;
        result.app_container = container != 0;
        result.ui_access = ui_access != 0;
        result.elevated = elevation.TokenIsElevated != 0;
        std::array<std::uint8_t, 256> bytes{};
        DWORD used{};
        if (!variable(TokenAppContainerSid, bytes, used))
            return false;
        if (used < sizeof(TOKEN_APPCONTAINER_INFORMATION))
            return fail(ERROR_INVALID_DATA, TokenAppContainerSid);
        TOKEN_APPCONTAINER_INFORMATION package{};
        std::memcpy(&package, bytes.data(), sizeof(package));
        if (!sid(bytes, used, package.TokenAppContainer, result, true))
            return false;
        bytes.fill(0);
        if (!variable(TokenIntegrityLevel, bytes, used))
            return false;
        if (used < sizeof(TOKEN_MANDATORY_LABEL))
            return fail(ERROR_INVALID_DATA, TokenIntegrityLevel);
        TOKEN_MANDATORY_LABEL label{};
        std::memcpy(&label, bytes.data(), sizeof(label));
        if (!sid(bytes, used, label.Label.Sid, result, false))
            return false;
        bytes.fill(0);
        if (!variable(TokenCapabilities, bytes, used))
            return false;
        if (used < sizeof(DWORD))
            return fail(ERROR_INVALID_DATA, TokenCapabilities);
        std::memcpy(&result.capability_count, bytes.data(), sizeof(DWORD));
        result.token_error = ERROR_SUCCESS;
        result.failed_information_class = 0;
        return true;
    }
};
Query::Token captureToken() noexcept {
    TokenCapture capture;
    HANDLE thread_token{};
    capture.result.thread_called = true;
    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &thread_token)) {
        capture.result.thread_error = ERROR_SUCCESS;
        capture.result.thread_close_called = true;
        capture.result.thread_close_error = CloseHandle(thread_token) ? ERROR_SUCCESS : GetLastError();
        return std::move(capture.result);
    }
    capture.result.thread_error = GetLastError();
    if (thread_token || capture.result.thread_error != ERROR_NO_TOKEN)
        return std::move(capture.result);
    capture.result.token_query_called = true;
    capture.result.failed_information_class = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &capture.process_token)) {
        capture.result.token_error = GetLastError();
        return std::move(capture.result);
    }
    try {
        capture.result.complete = capture.read();
    } catch (const std::bad_alloc&) {
        capture.result.token_error = ERROR_NOT_ENOUGH_MEMORY;
    } catch (...) {
        capture.result.token_error = ERROR_GEN_FAILURE;
    }
    capture.result.token_close_called = true;
    capture.result.token_close_error = CloseHandle(capture.process_token) ? ERROR_SUCCESS : GetLastError();
    return std::move(capture.result);
}

struct Native final {
    HKEY key{};
    bool attempted{}, owned{}, close_attempted{};
    Query::LocationResult location(const std::uint32_t access) noexcept {
        if (attempted || access != KEY_READ)
            return {static_cast<std::uint32_t>(E_INVALIDARG), 0, false};
        attempted = true;
        const auto status = GetAppContainerRegistryLocation(KEY_READ, &key);
        // Output ownership is defined only on S_OK. Do not use or close an
        // anomalous failure output or a predefined/invalid pseudo handle.
        const auto raw = reinterpret_cast<std::uintptr_t>(key);
        const auto low = raw & 0xffffffffULL;
        const bool pseudo = raw == ~std::uintptr_t{0} || (low >= 0x80000000ULL && low <= 0x800000ffULL);
        if ((status != S_OK && key) || (key && pseudo))
            return {static_cast<std::uint32_t>(status), 0, true};
        owned = status == S_OK && key;
        return {static_cast<std::uint32_t>(status), owned ? 1U : 0U, false};
    }
    Query::NameResult name(const Query::Handle handle, const std::span<std::uint8_t> buffer) noexcept {
        if (handle != 1 || !owned || close_attempted || buffer.size() > Query::maximum_query_bytes)
            return {0xc0000008U, ERROR_INVALID_HANDLE, 0};
        ULONG used{};
        const auto status =
            NtQueryKey(key, 3, buffer.empty() ? nullptr : buffer.data(), static_cast<ULONG>(buffer.size()), &used);
        return {static_cast<std::uint32_t>(status), status == 0 ? 0U : RtlNtStatusToDosError(status), used};
    }
    std::uint32_t close(const Query::Handle handle) noexcept {
        if (handle != 1 || !owned || close_attempted)
            return ERROR_INVALID_HANDLE;
        close_attempted = true;
        // A failed close is retained and never retried by this port/destructor.
        return static_cast<std::uint32_t>(RegCloseKey(key));
    }
};
} // namespace
AppContainerRegistryLocationQuery::Calls appContainerRegistryLocationNativeCalls() {
    auto native = std::make_shared<Native>();
    return {captureToken, [native](const auto access) { return native->location(access); },
            [native](const auto key, const auto buffer) { return native->name(key, buffer); },
            [native](const auto key) { return native->close(key); }};
}
} // namespace music_player::sandbox_probe

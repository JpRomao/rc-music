#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace music_player::sandbox_probe {
inline constexpr std::size_t maximum_request_bytes = 16 * 1024;
inline constexpr std::uint32_t all_checks = 0x7fff;
enum Check : std::uint32_t {
    Stdio = 1,
    AppContainer = 2,
    PackageSid = 4,
    ZeroCapabilities = 8,
    LowIntegrity = 16,
    InputRead = 32,
    InputWriteDenied = 64,
    AmbientReadDenied = 128,
    AmbientWriteDenied = 256,
    RuntimeWriteDenied = 512,
    JobChildDenied = 1024,
    LoopbackDenied = 2048,
    RuntimeDaclDenied = 4096,
    RuntimeOwnerDenied = 8192,
    RuntimeDeleteDenied = 16384,
};
struct Request final {
    std::vector<std::uint8_t> sid;
    std::uint64_t input_handle{};
    std::array<std::uint8_t, 32> challenge{};
    std::wstring sentinel;
    std::wstring runtime;
    std::uint16_t port{};
    std::uint16_t udp_port{};
    bool operator==(const Request&) const = default;
};
struct Report final {
    std::uint32_t passed{};
    std::uint32_t capability_count{std::numeric_limits<std::uint32_t>::max()};
    std::uint32_t integrity_rid{};
    // Token, input write, ambient read/write, runtime write, child, network, stdio.
    std::array<std::uint32_t, 11> errors{};
    std::uint32_t udp_error{std::numeric_limits<std::uint32_t>::max()};
    bool operator==(const Report&) const = default;
};
inline bool validHoldNonce(const std::wstring_view value) noexcept {
    if (value.size() != 32)
        return false;
    for (const auto unit : value)
        if (!((unit >= L'0' && unit <= L'9') || (unit >= L'a' && unit <= L'f')))
            return false;
    return true;
}
inline void put(std::vector<std::uint8_t>& bytes, std::uint64_t value, std::size_t count) {
    for (std::size_t index = 0; index < count; ++index)
        bytes.push_back(static_cast<std::uint8_t>(value >> (index * 8)));
}
inline std::uint64_t take(std::span<const std::uint8_t> bytes, std::size_t& at, std::size_t count) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < count; ++index)
        value |= std::uint64_t{bytes[at++]} << (index * 8);
    return value;
}
inline bool validPath(const std::wstring& path) {
    if (path.size() < 3 || path.size() > 2048 ||
        !((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) || path[1] != L':' ||
        (path[2] != L'/' && path[2] != L'\\'))
        return false;
    for (const auto unit : path)
        if (unit < L' ')
            return false;
    return true;
}
inline std::vector<std::uint8_t> encode(const Request& request) {
    if (request.sid.size() < 8 || request.sid.size() > 68 || !request.input_handle ||
        request.input_handle > static_cast<std::uint64_t>(std::numeric_limits<std::intptr_t>::max()) || !request.port ||
        !request.udp_port || !validPath(request.sentinel) || !validPath(request.runtime))
        return {};
    std::vector<std::uint8_t> bytes{'R', 'C', 'M', 'S', 'B', 'X', '0', '1'};
    put(bytes, 2, 2);
    put(bytes, request.sid.size(), 2);
    put(bytes, request.input_handle, 8);
    put(bytes, request.sentinel.size(), 2);
    put(bytes, request.runtime.size(), 2);
    put(bytes, request.port, 2);
    put(bytes, request.udp_port, 2);
    bytes.insert(bytes.end(), request.challenge.begin(), request.challenge.end());
    bytes.insert(bytes.end(), request.sid.begin(), request.sid.end());
    for (const auto& path : {request.sentinel, request.runtime})
        for (const auto unit : path)
            put(bytes, static_cast<std::uint16_t>(unit), 2);
    return bytes.size() <= maximum_request_bytes ? bytes : std::vector<std::uint8_t>{};
}
inline std::optional<Request> decodeRequest(std::span<const std::uint8_t> bytes) {
    constexpr std::array<std::uint8_t, 8> magic{'R', 'C', 'M', 'S', 'B', 'X', '0', '1'};
    if (bytes.size() < 60 || bytes.size() > maximum_request_bytes)
        return {};
    for (std::size_t index = 0; index < magic.size(); ++index)
        if (bytes[index] != magic[index])
            return {};
    std::size_t at = 8;
    if (take(bytes, at, 2) != 2)
        return {};
    const auto sid_size = take(bytes, at, 2);
    Request request;
    request.input_handle = take(bytes, at, 8);
    const auto sentinel_size = take(bytes, at, 2);
    const auto runtime_size = take(bytes, at, 2);
    request.port = static_cast<std::uint16_t>(take(bytes, at, 2));
    request.udp_port = static_cast<std::uint16_t>(take(bytes, at, 2));
    if (sid_size < 8 || sid_size > 68 || sentinel_size > 2048 || runtime_size > 2048 ||
        bytes.size() != 60 + sid_size + 2 * (sentinel_size + runtime_size))
        return {};
    for (auto& unit : request.challenge)
        unit = bytes[at++];
    for (std::size_t index = 0; index < sid_size; ++index)
        request.sid.push_back(bytes[at++]);
    for (const auto count : {sentinel_size, runtime_size}) {
        std::wstring path;
        for (std::size_t index = 0; index < count; ++index)
            path.push_back(static_cast<wchar_t>(take(bytes, at, 2)));
        if (request.sentinel.empty())
            request.sentinel = std::move(path);
        else
            request.runtime = std::move(path);
    }
    return !encode(request).empty() ? std::optional<Request>{std::move(request)} : std::nullopt;
}
inline std::vector<std::uint8_t> encode(const Report& report) {
    std::vector<std::uint8_t> bytes{'R', 'C', 'M', 'S', 'B', 'R', '0', '1'};
    put(bytes, 2, 2);
    put(bytes, report.passed, 4);
    put(bytes, report.capability_count, 4);
    put(bytes, report.integrity_rid, 4);
    for (const auto error : report.errors)
        put(bytes, error, 4);
    put(bytes, report.udp_error, 4);
    return bytes;
}
inline std::optional<Report> decodeReport(std::span<const std::uint8_t> bytes) {
    constexpr std::array<std::uint8_t, 8> magic{'R', 'C', 'M', 'S', 'B', 'R', '0', '1'};
    if (bytes.size() != 70)
        return {};
    for (std::size_t index = 0; index < magic.size(); ++index)
        if (bytes[index] != magic[index])
            return {};
    std::size_t at = 8;
    if (take(bytes, at, 2) != 2)
        return {};
    Report report;
    report.passed = static_cast<std::uint32_t>(take(bytes, at, 4));
    report.capability_count = static_cast<std::uint32_t>(take(bytes, at, 4));
    report.integrity_rid = static_cast<std::uint32_t>(take(bytes, at, 4));
    for (auto& error : report.errors)
        error = static_cast<std::uint32_t>(take(bytes, at, 4));
    report.udp_error = static_cast<std::uint32_t>(take(bytes, at, 4));
    return !(report.passed & ~all_checks) ? std::optional<Report>{report} : std::nullopt;
}
} // namespace music_player::sandbox_probe

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace music_player::sandbox_registry_location {
// Test-only transport. A valid frame, SID or nonce authenticates no process and
// conveys no profile provenance, access grant or authority to change policy.
using ProfileSid = std::array<std::uint8_t, 40>;
using Nonce = std::array<std::uint8_t, 32>;
inline constexpr std::size_t maximum_frame_bytes = 68 * 1024;
inline constexpr std::size_t maximum_native_name_units = 32767;
inline constexpr std::size_t request_frame_bytes = 84;
inline constexpr std::size_t reply_header_bytes = 144;

struct Request final {
    ProfileSid package_sid{};
    Nonce nonce{};
    bool operator==(const Request&) const = default;
};
enum class Outcome : std::uint8_t { RefusedToken = 1, NativeApiError = 2, CapturedName = 3, CloseUnknown = 4 };
struct TokenErrors final {
    // Absence means not observed, not error zero. All 32 raw bits are retained.
    std::optional<std::uint32_t> token_query_error{}, failed_information_class{}, process_token_close{},
        thread_token_open{}, thread_token_close{};
    bool operator==(const TokenErrors&) const = default;
};
struct Reply final {
    Outcome outcome{Outcome::RefusedToken};
    Nonce nonce{};
    std::optional<ProfileSid> session_package_sid{};
    TokenErrors token_errors{};
    // HRESULT and NTSTATUS are bit patterns, without sign conversion or
    // translation. Both native-name query stages keep their separate results.
    std::optional<std::uint32_t> registry_location_hresult{}, name_size_status{}, name_size_win32{},
        name_size_required{}, name_read_status{}, name_read_win32{}, name_read_required{}, key_close_error{};
    bool uncertain_output{false};
    // Counted UTF-16 locator only: no terminator, embedded NUL or malformed
    // surrogate sequence. This is not a persistent/native object identity.
    std::u16string native_name{};
    bool operator==(const Reply&) const = default;
};

// Version-one binary little-endian frames. Request is fixed-size; Reply has a
// fixed header and a counted name. Bounds and UTF-16 are checked before copying
// incoming variable data. Invalid input returns no frame/value, never a prefix.
[[nodiscard]] std::vector<std::uint8_t> encodeRequest(const Request& value);
[[nodiscard]] std::optional<Request> decodeRequest(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::vector<std::uint8_t> encodeReply(const Reply& value);
[[nodiscard]] std::optional<Reply> decodeReply(std::span<const std::uint8_t> bytes);
// Correlation only, including for a RefusedToken reply with a captured SID.
// Missing session SID cannot correlate identity. Caller authenticates the peer.
[[nodiscard]] bool matchesRequest(const Request& request, const Reply& reply) noexcept;
} // namespace music_player::sandbox_registry_location

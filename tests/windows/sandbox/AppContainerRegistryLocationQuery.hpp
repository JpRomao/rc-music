#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace music_player::sandbox_probe {
// Pure test-only orchestration of the contextual Registry-location query.
// A supplied SID/token DTO is diagnostic input, never creation provenance or
// permission to query another profile, harden it or acknowledge its cleanup.
class AppContainerRegistryLocationQuery final {
  public:
    using Handle = std::uintptr_t; // Opaque port-owned token, not a native HKEY/ID.
    static constexpr std::uint32_t not_called = std::numeric_limits<std::uint32_t>::max();
    static constexpr std::uint32_t key_read = 0x00020019;
    static constexpr std::uint32_t low_integrity_rid = 0x1000, no_thread_token = 1008;
    static constexpr std::uint32_t maximum_name_units = 32767;
    static constexpr std::uint32_t maximum_query_bytes = 4 + 2 * maximum_name_units;
    struct Token final {
        bool complete{false}, primary{false}, app_container{false}, elevated{false}, ui_access{false};
        std::vector<std::uint8_t> package_sid{};
        std::uint32_t capability_count{not_called}, integrity_rid{};
        std::uint32_t token_error{not_called}, failed_information_class{not_called};
        std::uint32_t token_close_error{not_called}, thread_error{not_called}, thread_close_error{not_called};
        // Raw UINT32_MAX is meaningful when the corresponding call was made.
        bool token_query_called{false}, token_close_called{false}, thread_called{false}, thread_close_called{false};
    };
    struct LocationResult final {
        std::uint32_t hresult{not_called}; // Raw HRESULT bits, S_OK is exactly zero.
        Handle owned_key{};
        // An undefined/anomalous output stays with the port until process exit.
        // In particular, failure + raw HKEY is not an ownership transfer.
        bool uncertain_output{false};
    };
    struct NameResult final {
        std::uint32_t ntstatus{not_called}, win32_error{not_called}, bytes{};
    };
    struct Calls final {
        std::function<Token()> capture_token{};
        std::function<LocationResult(std::uint32_t desired_access)> get_location{};
        // Empty span requests the size. Then at most one bounded data read.
        // Returned bytes is NtQueryKey's ResultLength, not a consumed name.
        std::function<NameResult(Handle owned_key, std::span<std::uint8_t> output)> query_name{};
        std::function<std::uint32_t(Handle owned_key)> close_key{};
    };
    enum class Outcome { RefusedToken, NativeApiError, CapturedName, CloseUnknown };
    enum class Reason {
        None,
        MissingCall,
        ExpectedSid,
        Token,
        Location,
        UncertainOutput,
        NameSize,
        Bounds,
        NameRead,
        NameFormat,
        Close,
        Exception
    };
    struct Result final {
        Outcome outcome{Outcome::RefusedToken};
        Reason reason{Reason::MissingCall};
        Token token{};
        bool token_called{false}, location_called{false}, size_called{false}, read_called{false}, close_called{false};
        bool token_returned{false}, location_returned{false}, size_returned{false}, read_returned{false},
            close_returned{false};
        LocationResult location{};
        NameResult size_query{}, read_query{};
        std::uint32_t close_error{not_called};
        // A bounded observation survives a later failed close. It does not
        // upgrade CloseUnknown to CapturedName or acknowledge any cleanup.
        std::u16string native_name{};
    };
    AppContainerRegistryLocationQuery() = delete;
    // One synchronous attempt per invocation, with no callback/Native binding
    // in construction, no retries and no implicit/destructor close or ACK.
    // Require expected canonical 40-byte S-1-15-2 SID, matching complete Low
    // primary AppContainer token, zero capabilities and no thread token. Only
    // a qualified S_OK ownership transfer can be queried/closed once. A failed
    // close remains unknown and is never retried. Names are counted UTF-16
    // locators, not persistent IDs, binding proof or security-policy approval.
    [[nodiscard]] static Result execute(std::span<const std::uint8_t> expected_sid, const Calls& calls) noexcept;
};
} // namespace music_player::sandbox_probe

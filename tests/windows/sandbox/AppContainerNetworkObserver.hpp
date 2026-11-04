#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace music_player::sandbox_probe {
class AppContainerNetworkObserver final {
  public:
    using Handle = std::uintptr_t;
    using Bytes = std::vector<std::uint8_t>;
    static constexpr std::uint32_t not_called = std::numeric_limits<std::uint32_t>::max();
    static constexpr std::uint64_t maximum_window_ticks = 15ULL * 10'000'000;
    static constexpr std::uint32_t loopback_ipv4 = 0x7f000001;
    static constexpr std::uint32_t capability_drop_keyword = 4;
    enum class State { Idle, Observing, Stopped, Unavailable, StopUnconfirmed };
    enum class Reason {
        None,
        InvalidConfiguration,
        MissingCall,
        AppId,
        Engine,
        CollectionQuery,
        CollectionType,
        CollectionDisabled,
        KeywordQuery,
        KeywordType,
        KeywordDisabled,
        Subscribe,
        Unsubscribe,
        Close,
        Exception
    };
    struct Config final {
        std::wstring executable{};
        Bytes package_sid{}, user_sid{};
        std::uint16_t remote_port{};
        std::uint64_t begin_filetime{}, end_filetime{};
    };
    enum HeaderField : std::uint32_t {
        IpVersion = 1,
        Protocol = 2,
        RemoteAddress = 4,
        RemotePort = 8,
        AppId = 16,
        UserSid = 32,
        PackageSid = 64
    };
    static constexpr std::uint32_t required_fields = 0x7f;
    enum class EventKind { CapabilityDrop, ClassifyDrop, CapabilityAllow, ClassifyAllow, Other };
    // Borrowed callback views. No bytes from unrelated traffic are retained.
    struct EventView final {
        EventKind kind{EventKind::Other};
        std::uint32_t fields{};
        std::uint64_t timestamp{};
        std::uint32_t ip_version{}, remote_ipv4{}; // normalized IPv4=4, host-order address
        std::uint8_t protocol{};
        std::uint16_t remote_port{};
        std::span<const std::uint8_t> app_id{}, user_sid{}, package_sid{};
        std::uint32_t capability_id{not_called}; // documented values 0, 1 or 2 only
        std::uint64_t filter_id{};
        std::int32_t is_loopback{}; // exact native TRUE (1), not merely nonzero
    };
    struct Match final {
        std::uint64_t timestamp{}, filter_id{};
        std::uint32_t capability_id{};
    };
    struct Report final {
        State state{State::Idle};
        Reason reason{Reason::None};
        bool available{false}, matched{false}, context_retained{false};
        std::uint32_t app_id_error{not_called}, open_error{not_called}, query_error{not_called};
        std::uint32_t keyword_query_error{not_called};
        std::uint32_t subscribe_error{not_called}, unsubscribe_error{not_called}, close_error{not_called};
        bool value_is_uint32{false};
        std::uint32_t collection_value{};
        bool keywords_is_uint32{false};
        std::uint32_t keywords_value{};
        std::optional<Match> first_match{};
    };
    struct PreflightReport final {
        bool available{false}, context_retained{false};
        Reason reason{Reason::None};
        std::uint32_t open_error{not_called}, query_error{not_called}, close_error{not_called};
        std::uint32_t keyword_query_error{not_called};
        bool value_is_uint32{false};
        std::uint32_t collection_value{};
        bool keywords_is_uint32{false};
        std::uint32_t keywords_value{};
    };
    struct AppIdResult final {
        std::uint32_t error{31};
        Bytes bytes{};
    };
    struct OpenResult final {
        std::uint32_t error{31};
        Handle handle{};
    };
    struct OptionResult final {
        std::uint32_t error{31};
        bool is_uint32{false};
        std::uint32_t value{};
    };
    struct SubscribeResult final {
        std::uint32_t error{31};
        Handle handle{};
    };
    struct SubscriptionFilter final {
        std::span<const std::uint8_t> app_id{};
        std::uint32_t remote_ipv4{loopback_ipv4};
        std::uint8_t protocol{6};
        std::uint16_t remote_port{};
        EventKind kind{EventKind::CapabilityDrop};
        std::uint64_t begin_filetime{}, end_filetime{};
    };
    using Callback = void (*)(void*, const EventView&) noexcept;
    struct Calls final {
        std::function<AppIdResult(std::wstring_view)> app_id{};
        std::function<OpenResult()> open{};
        std::function<OptionResult(Handle)> get_collection{};
        std::function<OptionResult(Handle)> get_keywords{};
        std::function<SubscribeResult(Handle, const SubscriptionFilter&, Callback, void*)> subscribe{};
        // Success means all callbacks have returned and no new callback can start.
        std::function<std::uint32_t(Handle, Handle)> unsubscribe{};
        std::function<std::uint32_t(Handle)> close{};
    };

    // These factories bind functions only. Only start/preflight call the OS.
    [[nodiscard]] static Calls systemCalls();
    [[nodiscard]] static PreflightReport preflight(Calls calls) noexcept;
    [[nodiscard]] static PreflightReport nativePreflight() noexcept;
    AppContainerNetworkObserver(Config config, Calls calls);
    ~AppContainerNetworkObserver();
    AppContainerNetworkObserver(const AppContainerNetworkObserver&) = delete;
    AppContainerNetworkObserver& operator=(const AppContainerNetworkObserver&) = delete;
    [[nodiscard]] Report start() noexcept;
    [[nodiscard]] Report snapshot() const noexcept;
    [[nodiscard]] Report stop() noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::sandbox_probe

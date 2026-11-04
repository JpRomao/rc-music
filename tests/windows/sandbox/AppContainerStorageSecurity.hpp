#pragma once

#include <QByteArray>

#include <cstdint>
#include <functional>
#include <limits>
#include <span>

namespace music_player::sandbox_probe {
// Read-only test-probe adapter. Handles are borrowed and are never closed.
// Error::None means a bounded structural read, not valid ACL policy or MIC.
class AppContainerStorageSecurity final {
  public:
    using Handle = std::uintptr_t;
    static constexpr std::uint32_t not_called = std::numeric_limits<std::uint32_t>::max();
    static constexpr std::uint64_t maximum_sd_bytes = 64 * 1024, maximum_sid_bytes = 68;
    static constexpr std::uint32_t minimum_sd_bytes = 20;
    enum class Object { File, Key };
    enum class Dacl { Unknown, Absent, Null, Present };
    enum class Error { None, InvalidHandle, InvalidObject, MissingCall, SizeQuery, Bounds, Read, InvalidSD, Exception };
    struct NativeResult final {
        std::uint32_t error{not_called}, bytes{};
    };
    struct Calls final {
        std::function<NativeResult(Object, Handle)> size_query{};
        std::function<NativeResult(Object, Handle, std::span<std::uint8_t>)> read{};
    };
    struct Result final {
        Error error{Error::InvalidSD};
        std::uint32_t query_error{not_called}, read_error{not_called};
        std::uint32_t required_bytes{}, used_bytes{};
        QByteArray raw_sd{}, owner_sid{};
        Dacl dacl{Dacl::Unknown};
        bool dacl_protected{false};
        bool sacl_not_queried{true};
    };
    // Binding and construction perform no native calls.
    [[nodiscard]] static Calls systemCalls();
    explicit AppContainerStorageSecurity(Calls calls);
    AppContainerStorageSecurity(const AppContainerStorageSecurity&) = delete;
    AppContainerStorageSecurity& operator=(const AppContainerStorageSecurity&) = delete;
    // One size query and at most one read, without retry or path fallback.
    // maximum_bytes bounds raw SD plus the separately copied owner SID.
    [[nodiscard]] Result read(Object object, Handle borrowed, std::uint64_t maximum_bytes) const noexcept;

  private:
    Calls calls_;
};
} // namespace music_player::sandbox_probe

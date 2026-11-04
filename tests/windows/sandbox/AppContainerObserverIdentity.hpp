#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace music_player::sandbox_probe {
// Probe-only primary-process token inspection. Captures never adjust privileges,
// impersonate, inspect a thread token or provision any AppContainer resources.
class AppContainerObserverIdentity final {
  public:
    using Handle = std::uintptr_t;
    using Bytes = std::vector<std::uint8_t>;
    static constexpr std::uint32_t not_called = std::numeric_limits<std::uint32_t>::max();
    static constexpr std::size_t maximum_sid_bytes = 68;
    static constexpr std::size_t maximum_token_bytes = 1024 * 1024;
    static constexpr std::size_t maximum_groups = 4096;
    static constexpr std::size_t maximum_privileges = 1024;
    static constexpr std::size_t maximum_privilege_name_units = 256;
    static constexpr std::uint32_t medium_integrity_rid = 0x2000;
    static constexpr std::uint32_t high_integrity_rid = 0x3000;
    enum class ElevationType { Unknown, Default, Full, Limited };
    struct Group final {
        Bytes sid{};
        std::uint32_t attributes{};
    };
    struct Privilege final {
        std::wstring name{};
        std::uint32_t attributes{};
    };
    struct Snapshot final {
        bool complete{false};
        std::uint32_t error{not_called};
        std::uint32_t failed_information_class{};
        Bytes user_sid{};
        std::uint32_t session_id{};
        bool elevated{false};
        ElevationType elevation_type{ElevationType::Unknown};
        std::uint32_t integrity_rid{};
        bool ui_access{false}, app_container{false};
        Bytes app_container_sid{};
        std::uint32_t capability_count{};
        bool administrators_group_present{false};
        std::uint32_t administrators_group_attributes{};
        std::vector<Group> restricted_sids{};
        std::vector<Privilege> enabled_privileges{};
    };

    [[nodiscard]] static Snapshot captureProcessToken(Handle borrowed_process) noexcept;
    [[nodiscard]] static Snapshot captureCurrentToken() noexcept;
    [[nodiscard]] static bool normalDriver(const Snapshot& value) noexcept;
    [[nodiscard]] static bool elevatedObserver(const Snapshot& value) noexcept;
    // This binds the captured driver, not the elevated observer's user account.
    [[nodiscard]] static bool matchesCaller(const Snapshot& driver, std::span<const std::uint8_t> driver_user_sid,
                                            std::uint32_t driver_session_id) noexcept;
};
} // namespace music_player::sandbox_probe

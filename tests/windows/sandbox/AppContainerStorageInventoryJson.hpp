#pragma once

#include "AppContainerStorageInventory.hpp"

#include <QByteArray>

#include <cstdint>

namespace music_player::sandbox_probe {
// Pure, test-only diagnostic serializer. No I/O, paths, native handles, owner
// calls or new persistence format for the player. The caller records evidence.
class AppContainerStorageInventoryJson final {
  public:
    using Inventory = AppContainerStorageInventory;
    static constexpr std::uint64_t maximum_owner_sid_bytes = 68;
    static constexpr std::uint64_t maximum_json_bytes = 12 * 1024 * 1024;
    enum class Error { None, InvalidEnum, InvalidReport, Bounds, Exception };
    struct Result final {
        Error error{Error::InvalidReport};
        QByteArray bytes{};
        // Recomputed owner + raw SD + UTF-16 relative-name bytes. Never trust
        // Report::total_bytes to bound allocation of diagnostic payloads.
        std::uint64_t payload_bytes{};
    };
    // Snapshot validation, later revalidation and close remain separate facts.
    // Partial/Unknown metadata and raw errors/flags are preserved, without
    // interpreting SDs or granting cleanup, policy or identity authority.
    // Invalid enum/order/text or limits refuse the whole encoding, no truncation.
    // All uint64 fields use decimal strings, preserving values above 2^53.
    [[nodiscard]] static Result encode(const Inventory::Report& report, Inventory::ValidationResult post_validation,
                                       Inventory::CloseResult closure) noexcept;
};
} // namespace music_player::sandbox_probe

#pragma once

#include <QByteArrayView>
#include <QString>

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>

namespace music_player::sandbox_probe {
// Test-only, one-run evidence directory. This is not profile provenance,
// permission to create a profile, or an ACK for a profile/read-session fence.
class AppContainerStorageProbeEvidence final {
  public:
    using Handle = std::uintptr_t;
    static constexpr qsizetype maximum_receipt_bytes = 12 * 1024 * 1024;
    static constexpr std::uint32_t not_called = std::numeric_limits<std::uint32_t>::max();
    enum class Leaf { Before, Profile, Inventory, Close, Cleanup, Result };
    enum class Phase {
        None,
        Input,
        Drive,
        Ancestors,
        EvidenceDirectory,
        EvidencePin,
        JournalDirectory,
        Open,
        Write,
        Flush,
        Close,
        Complete
    };
    struct Result final {
        bool ok{false};
        std::uint32_t error{not_called};
        Phase phase{Phase::None};
        std::uint32_t close_error{not_called}, bytes_written{};
    };
    struct WriteResult final {
        std::uint32_t error{not_called}, bytes{};
    };
    // Trusted fault-injection seam for an already-owned receipt handle only.
    // There is no open/path override. Each callback returns raw Win32 results.
    struct Calls final {
        std::function<WriteResult(Handle, QByteArrayView)> write{};
        std::function<std::uint32_t(Handle)> flush{}, close{};
    };
    [[nodiscard]] static Calls systemCalls(); // Binds only; no native I/O.
    AppContainerStorageProbeEvidence(QString compiled_workspace, QString evidence);
    AppContainerStorageProbeEvidence(QString compiled_workspace, QString evidence, Calls calls);
    ~AppContainerStorageProbeEvidence();
    AppContainerStorageProbeEvidence(const AppContainerStorageProbeEvidence&) = delete;
    AppContainerStorageProbeEvidence& operator=(const AppContainerStorageProbeEvidence&) = delete;

    // Metadata access only, deep-owned even when the caller used fromRawData.
    [[nodiscard]] const QString& directory() const noexcept;
    [[nodiscard]] const QString& journalDirectory() const noexcept;
    [[nodiscard]] QString path(Leaf leaf) const;
    // One creation attempt. Requires exact compiled out/product prefix,
    // fixed local drive, safe ancestors and a fresh private directory. Pins of
    // the evidence ancestors/leaf stay alive until destruction.
    [[nodiscard]] Result create() noexcept;
    // Fresh fixed profile-journal child, protected two-ACE DACL with flags 0.
    // No existing object/ACL is adopted or repaired. Does not pin the journal
    // with restrictive share modes that would block the store's atomic rename.
    [[nodiscard]] Result createJournal() noexcept;
    // Only six fixed leaves, nonempty bytes <= maximum_receipt_bytes. Admission
    // failures precede native I/O and do not consume a leaf. At most one native
    // acquisition attempt per admitted leaf, even after error/partial write.
    // Explicitly checks write, flush and close; no overwrite/delete/retry.
    // Short successful writes preserve raw error 0 plus Write/bytes_written.
    // Failed close retains the uncertain handle until process exit; neither
    // destruction nor another writeNew call retries it or invents an ACK.
    [[nodiscard]] Result writeNew(Leaf leaf, QByteArrayView bytes) noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::sandbox_probe

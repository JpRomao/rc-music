#pragma once

#include "m0/ports/DownloadExecution.hpp"

#include <cstddef>
#include <memory>

namespace music_player::m0 {
class PlayerPresenter;
struct PlayerDownloadImportLimits final {
    std::size_t max_receipts{128};
    // Logical bytes of IDs/native paths; not all container/allocator overhead.
    std::size_t max_retained_bytes{1024 * 1024};
};
enum class PlayerDownloadImportPersistence { Volatile, Session };

// Application-thread handoff of trusted Published results only. No filesystem,
// decoder, discovery or playback work runs here. Lexical checks do NOT prove
// bytes/publication/ownership: the executor and composition must supply that.
// Committed receipts bind job ID to path without eviction for this owner's
// lifetime in Volatile mode, including after manual queue removal. Session mode
// commits receipts with the queue, waits for matching Saved and recognizes loaded
// receipts without reading/importing paths. Uncommitted refusal/exception releases
// its reservation; a committed but unsaved receipt survives explicit retry.
// Session mode needs healthy persistence, and blocks new imports on recovered,
// invalid or future history until an explicit reconciliation flow exists.
// Construct/use/destroy
// on the presenter's thread. Borrowing service/owner must keep this adapter and
// model alive through callbacks; Qt row callbacks must not throw or delete them.
class PlayerDownloadImport final : public DownloadImport {
  public:
    explicit PlayerDownloadImport(
        PlayerPresenter& presenter, PlayerDownloadImportLimits limits = {},
        PlayerDownloadImportPersistence persistence = PlayerDownloadImportPersistence::Volatile);
    ~PlayerDownloadImport() override;
    PlayerDownloadImport(const PlayerDownloadImport&) = delete;
    PlayerDownloadImport& operator=(const PlayerDownloadImport&) = delete;
    [[nodiscard]] bool accept(const std::string& job_id, const std::filesystem::path& mp3_path) override;
    [[nodiscard]] bool supportsDurableRecovery() const override;
    [[nodiscard]] DownloadImportState begin(const std::string& job_id, const std::filesystem::path& mp3_path) override;
    [[nodiscard]] DownloadImportState pollImport(std::string_view job_id) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace music_player::m0
